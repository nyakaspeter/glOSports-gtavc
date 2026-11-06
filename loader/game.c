/* game.c -- Grand Theft Auto: Vice City, Rockstar's Android build
 * (libGTAVC.so, War Drum Studios, 2012; md5 0d10ceaf, with OBB 11): hooks and
 * fixes, carried over from glOSports-gta3 (the same War Drum layer) and
 * checked against this library. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

#include "android.h"
#include "so_util.h"
#include "util.h"

const char *game_so_name = "libGTAVC.so";
const char *const game_required_files[] = {
	"libGTAVC.so", "data/main.scm", NULL,
};

/* FileGetArchiveName answers no archive for the archive CFileMgr asks for
 * (0): the game then opens every file loose from the storage root (the OBB
 * and the APK's assets/ unpacked into game/) */
const JavaMethod game_java_methods[] = {
	COMMON_JAVA_METHODS,
	{ NULL, NULL },
};

/* The pad, straight to the WarGamepad layer the Java calls would feed. Type
 * 0 is an Xbox 360 pad (Handle360Controller), the GA36-MB's layout. */
static int WarGamepad_GetGamepadType(void)
{
	pad_update();
	return 0;
}

static int WarGamepad_GetGamepadButtons(void)
{
	return pad_buttons();
}

static SOFTFP float WarGamepad_GetGamepadAxis(int axis)
{
	return pad_axis(axis);
}

/* The HUD is laid out like the touch controls: the radar is drawn in the
 * touchscreen's radar button (CRadar::TransformRadarPointToScreenSpace),
 * clock, money, health, weapon and wanted level in its summary button,
 * the help box in its tut button (CHud::Draw), each a rectangle of floats x, y, width, height at +4..+16.
 * The loader places them as GTA SA's Xbox 360 layout does on this screen
 * (Adjustable.cfg's radar: centre 76, 370 and radius 44 in 640x448; the
 * stats block measured in the game): the radar bottom left, with SA's size,
 * the stats top right and the help box top left, in from the edges (the
 * help box sat right of the radar). The radar is drawn inside an
 * 8-pixel border of its button. */
static void **touchscreen;
static float *(*Touchscreen__GetRadarButton)(void *ts);
static float *(*Touchscreen__GetSummaryButton)(void *ts);
static float *(*Touchscreen__GetTutButton)(void *ts);

static void place_hud(void)
{
	if (!*touchscreen)
		return;
	float w = OS_ScreenGetWidth(), h = OS_ScreenGetHeight();
	float *r = Touchscreen__GetRadarButton(*touchscreen);
	if (r) {
		float radius = 44.0f, cx = 76.0f, cy = h - 84.0f;
		r[3] = r[4] = 2.0f * radius + 16.0f;
		r[1] = cx - r[3] / 2.0f;
		r[2] = cy - r[4] / 2.0f;
	}
	float *s = Touchscreen__GetSummaryButton(*touchscreen);
	if (s) {
		/* the block draws 4 pixels in from its button's top and right */
		s[1] = w - 30.0f - s[3];
		s[2] = 36.0f;
	}
	float *t = Touchscreen__GetTutButton(*touchscreen);
	if (t) {
		t[1] = 32.0f;
		t[2] = 36.0f;
	}
}

static int game_ProcessEvents(void)
{
	if (touchscreen)
		place_hud();
	return ProcessEvents();
}

/* Help texts name buttons with ~k~~ACTION~, filled from the PC joystick
 * binding of the action (ControlsManager: the mobile build feeds the pad
 * through the PC joystick layer, Handle360Controller turning pad buttons
 * into joystick buttons). Its text, FEC_JBO ("Joy %d"), is missing from
 * the mobile .gxt, so the prompts came out empty: name the joystick button
 * by the GA36-MB's (Xbox) label instead. Joystick button n is bit n-1 of
 * Handle360Controller's mask. */
static const char *const joy_names[] = {
	[1] = "A", [2] = "RT", [3] = "LT", [4] = "B", [5] = "LB", [6] = "RB",
	[7] = "X", [8] = "Y", [9] = "BACK", [12] = "START", [13] = "UP",
	[14] = "RIGHT", [15] = "DOWN", [16] = "LEFT", [20] = "L3", [21] = "R3",
	[22] = "UP",
};

/* appends the action's button to text (the game's version lists every
 * binding: keyboard, mouse and joystick) */
static void GetWideStringOfCommandKeys(char *mgr, uint16_t action, uint16_t *text, uint16_t size)
{
	int key = *(int *)(mgr + 3580 + action * 32);
	if (key <= 0 || key >= (int)(sizeof(joy_names) / sizeof(*joy_names)) || !joy_names[key])
		return;
	int i = 0;
	while (i < size && text[i])
		i++;
	for (const char *c = joy_names[key]; *c && i + 1 < size; c++)
		text[i++] = (uint8_t)*c;
	if (i < size)
		text[i] = 0;
}

/* Vice City's mobile texts name buttons with touch tokens (Tap ~h~::FIRE::~w~
 * to fire), drawn as touch-button icons (Touchscreen::GetSpriteForString).
 * Each touch button has the pad button that presses it in the default
 * layout (ButtonDefaultsTablet, 36-byte records: hud_shoot 4, i.e. B), so
 * the tokens become those buttons' names in the loaded text. TANKLEFT and
 * TANKRIGHT (no pad button: that aim is analog), SAVE (the save-spot icon
 * in the world) and SWAP (no touch button of its own) stay icons. The text
 * is CText's CData {chars, count}: the main text at +8, a mission's at +24;
 * a token is longer than its name, so a string only gets shorter. */
static const struct { const char *token, *name; } touch_tokens[] = {
	{ "FIRE", "B" }, { "ATTACK", "B" }, { "SPRINT", "A" }, { "LOCKON", "RB" },
	{ "CAR", "Y" }, { "DOOR", "Y" }, { "EXIT", "Y" }, { "ACCEL", "A" },
	{ "BRAKE", "X" }, { "HANDBRAKE", "RB" }, { "HORN", "R3" },
	{ "SUBMISSIONS", "L3" }, { "PHONE", "LB" }, { "BUY", "A" },
	{ "LEFT", "LEFT" }, { "RIGHT", "RIGHT" },
};

static int replace_touch_tokens(uint16_t *c, int n)
{
	int replaced = 0;
	for (int i = 0; c && i + 4 < n; i++) {
		if (c[i] != ':' || c[i + 1] != ':')
			continue;
		int e = i + 2;
		while (e + 1 < n && c[e] && c[e] != ':')
			e++;
		if (e + 1 >= n || c[e] != ':' || c[e + 1] != ':')
			continue;
		int len = e - (i + 2);
		for (size_t t = 0; t < sizeof(touch_tokens) / sizeof(*touch_tokens); t++) {
			const char *tok = touch_tokens[t].token;
			if ((int)strlen(tok) != len)
				continue;
			int k = 0;
			while (k < len && c[i + 2 + k] == (uint8_t)tok[k])
				k++;
			if (k < len)
				continue;
			/* name over the token, the rest of the string moved up */
			int end = e + 2, z = end;
			while (z < n && c[z])
				z++;
			const char *name = touch_tokens[t].name;
			int nl = strlen(name);
			for (int j = 0; j < nl; j++)
				c[i + j] = (uint8_t)name[j];
			memmove(c + i + nl, c + end, (z - end) * 2);
			for (int j = i + nl + (z - end); j < z; j++)
				c[j] = 0;
			replaced++;
			break;
		}
	}
	return replaced;
}

static uintptr_t trampoline(const char *sym)
{
	/* the function's first 8 bytes (position independent: checked for
	 * the functions used here), then a jump back past them */
	uintptr_t f = so_find_addr(sym) & ~1u;
	uint8_t *tr = mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (tr == MAP_FAILED)
		fatal_error("trampoline for %s: no memory", sym);
	memcpy(tr, (void *)f, 8);
	static const uint8_t ldr_pc[4] = { 0xdf, 0xf8, 0x00, 0xf0 }; /* ldr.w pc, [pc] */
	memcpy(tr + 8, ldr_pc, 4);
	*(uint32_t *)(tr + 12) = (f + 8) | 1;
	__builtin___clear_cache((char *)tr, (char *)tr + 16);
	return (uintptr_t)tr | 1;
}

static void (*CText__Load)(char *text);
static void (*CText__LoadMissionText)(char *text, char *name);

static void CText__Load_hook(char *text)
{
	CText__Load(text);
	log_printf("game: %d touch tokens named in the text\n",
		replace_touch_tokens(*(uint16_t **)(text + 8), *(int *)(text + 12)));
}

static void CText__LoadMissionText_hook(char *text, char *name)
{
	CText__LoadMissionText(text, name);
	replace_touch_tokens(*(uint16_t **)(text + 24), *(int *)(text + 28));
}

/* a bl in the game's code reaches +-16 MB: a jump to the loader's function
 * from a page right after the library */
static uint8_t *veneers;
static size_t veneers_used;

static uintptr_t veneer(uintptr_t target)
{
	if (!veneers) {
		uintptr_t want = (so_base + so_span + 0xfffff) & ~0xfffffu;
		veneers = mmap((void *)want, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (veneers == MAP_FAILED)
			fatal_error("veneer page: no memory");
	}
	uint8_t *v = veneers + veneers_used;
	static const uint8_t ldr_pc[4] = { 0xdf, 0xf8, 0x00, 0xf0 }; /* ldr.w pc, [pc] */
	memcpy(v, ldr_pc, 4);
	*(uint32_t *)(v + 4) = target | 1;
	veneers_used += 8;
	__builtin___clear_cache((char *)v, (char *)v + 8);
	return (uintptr_t)v;
}

/* the bl at off (checked against its original bytes) to target */
static int patch_bl(uintptr_t off, const uint8_t orig[4], uintptr_t target)
{
	if (memcmp((void *)(so_base + off), orig, 4) != 0)
		return 0;
	uintptr_t v = veneer(target);
	int32_t d = (int32_t)(v - (so_base + off + 4));
	if (d < -(1 << 24) || d >= (1 << 24))
		return 0;
	uint32_t imm = ((uint32_t)d >> 1) & 0xffffff;
	uint32_t S = (imm >> 23) & 1, I1 = (imm >> 22) & 1, I2 = (imm >> 21) & 1;
	uint32_t J1 = (!I1) ^ S, J2 = (!I2) ^ S;
	uint16_t hw[2] = {
		(uint16_t)(0xf000 | (S << 10) | ((imm >> 11) & 0x3ff)),
		(uint16_t)(0xd000 | (J1 << 13) | (J2 << 11) | (imm & 0x7ff)),
	};
	so_patch(off, hw, 4);
	return 1;
}

/* Frame Limiter: MainGameTick asks OS_ScreenSetRefresh for 30 Hz with the
 * limiter on (60 on two chip types), 0 with it off and 60 in the menu,
 * but on Android that is an empty function (bx lr, too short to hook), so
 * the setting did nothing. Its four calls come here: 30 fps with the
 * limiter on, the flips' pace otherwise. */
static void refresh_limited(unsigned hz)
{
	(void)hz;
	egl_frame_limit(30);
}

static void refresh_free(unsigned hz)
{
	(void)hz;
	egl_frame_limit(0);
}

static char *the_camera;

/* Lock-on follows GTA SA's rules (CPlayerPed::FindWeaponLockOnTarget,
 * EvaluateTarget, FindNextWeaponLockOnTarget, EvaluateNeighbouringTarget,
 * FindTargetPriority, LOSBlockedBetweenPeds). This game scored every
 * candidate in weapon range in front of the player, through walls too, by
 * distance plus 5 x the angle off. SA scores a candidate twice: off the
 * player's facing, and off the camera's heading when it is in front of
 * the camera (CSprite::CalcScreenCoors); the better counts. A score is
 * 1 - 0.785 x the angle off (none past about 73 degrees), over the fourth
 * root of the distance, times the target's priority (0.8 for its aiming priorities (IsThisPedAnAimingPriority), 0.1
 * for the rest; SA's own also rates cops, gangs and its mission targets,
 * which this game's peds don't carry), and counts only with a clear line
 * from neck to neck (buildings and objects). Switching to the next
 * target measures from the camera: the nearest angle from the current
 * target on the asked side, within 50 degrees, in line of sight. The
 * game's own pre-check (OurPedCanSeeThisOne: in front of the player,
 * 40 m), which SA doesn't have, is answered yes. */
typedef struct { float x, y, z; } CVector;
static SOFTFP int (*CPlayerPed__DoesTargetHaveToBeBroken)(char *ped, CVector pos, void *weapon);
static SOFTFP float (*CGeneral__GetATanOfXY)(float x, float y);
static int (*CSprite__CalcScreenCoors)(const float *in, float *out, float *w, float *h, int far_clip);
static int (*CWorld__GetIsLineOfSightClear)(const CVector *a, const CVector *b, int buildings, int vehicles,
	int peds, int objects, int dummies, int ignore_see_through, int ignore_some_objects);

static int return_1(void)
{
	return 1;
}

/* in range and holdable by the weapon */
static int can_lock(char *ped, char *target, float max_dist, float *dist)
{
	float *p = (float *)(ped + 52), *t = (float *)(target + 52);
	float dx = t[0] - p[0], dy = t[1] - p[1], dz = t[2] - p[2];
	*dist = sqrtf(dx * dx + dy * dy + dz * dz);
	if (*dist > max_dist)
		return 0;
	int slot = *(int8_t *)(ped + 1284);
	return !CPlayerPed__DoesTargetHaveToBeBroken(ped, (CVector){ t[0], t[1], t[2] }, ped + 1032 + slot * 24);
}

static float angle_off(float dx, float dy, float heading)
{
	float a = CGeneral__GetATanOfXY(dx, dy) - heading;
	while (a > 3.14159265f)
		a -= 6.28318531f;
	while (a < -3.14159265f)
		a += 6.28318531f;
	return a;
}

static float sa_score(float angle, float dist)
{
	float s = 1.0f - 0.785398f * fabsf(angle);
	if (s <= 0.0f)
		return 0.0f;
	return dist > 1.0f ? s / sqrtf(sqrtf(dist)) : s;
}

/* the best so far, if nothing blocks the line from neck to neck */
static void want(char *ped, char *target, char **best, float *best_score, float score)
{
	if (score <= *best_score)
		return;
	float *p = (float *)(ped + 52), *t = (float *)(target + 52);
	CVector from = { p[0], p[1], p[2] + 0.6f }, to = { t[0], t[1], t[2] + 0.6f };
	if (!CWorld__GetIsLineOfSightClear(&from, &to, 1, 0, 0, 1, 0, 1, 0))
		return;
	*best = target;
	*best_score = score;
}

/* heading: the player's facing (FindWeaponLockOnTarget) */
static SOFTFP void evaluate_target(char *ped, char *target, char **best, float *best_score,
	float max_dist, float heading, int priority, int level)
{
	float dist;
	if (!can_lock(ped, target, max_dist, &dist))
		return;
	float *p = (float *)(ped + 52), *t = (float *)(target + 52), *cf = (float *)(the_camera + 20);
	float score = sa_score(angle_off(t[0] - p[0], t[1] - p[1], heading), dist);
	float screen[3], w, h;
	if (CSprite__CalcScreenCoors(t, screen, &w, &h, 1)) {
		float s = sa_score(angle_off(t[0] - p[0], t[1] - p[1], CGeneral__GetATanOfXY(cf[0], cf[1])), dist);
		if (s > score)
			score = s;
	}
	if (score > 0.0f)
		want(ped, target, best, best_score, score * (priority ? 0.8f : 0.1f));
}

/* the game's heading here is from the player; SA's is from the camera to
 * the current target */
static SOFTFP void evaluate_neighbouring_target(char *ped, char *target, char **best, float *best_score,
	float max_dist, float heading, int left, int priority, int level)
{
	float dist;
	if (!can_lock(ped, target, max_dist, &dist))
		return;
	float *t = (float *)(target + 52), *cf = (float *)(the_camera + 20), *cp = (float *)(the_camera + 52);
	char *current = *(char **)(ped + 1288);
	float *c = current ? (float *)(current + 52) : NULL;
	float from_camera = c ? CGeneral__GetATanOfXY(c[0] - cp[0], c[1] - cp[1]) : CGeneral__GetATanOfXY(cf[0], cf[1]);
	float a = angle_off(t[0] - cp[0], t[1] - cp[1], from_camera);
	if (fabsf(a) >= 0.872665f || (left ? a <= 0.0f : a >= 0.0f))
		return;
	want(ped, target, best, best_score, -fabsf(a));
}

/* While locked on, CPlayerPed::ProcessPlayerWeapon moves the lock to the
 * next target with CPad::ShiftTargetLeft/RightJustDown (their only
 * callers): left trigger, right trigger or left shoulder just pressed,
 * which the pad layer doesn't seem to set. They also answer a flick of
 * the right stick (x past half, from inside half in the pad's previous
 * state, +46); the camera follows the target meanwhile. +240 is the pad's
 * disabled flag. */
static int just_down(int16_t *pad, int i)
{
	return pad[i] && !pad[i + 21];
}

static int shift_target_left(int16_t *pad)
{
	if (*(uint16_t *)((char *)pad + 240))
		return 0;
	return just_down(pad, 5) || (pad[2] <= -64 && pad[23] > -64);
}

static int shift_target_right(int16_t *pad)
{
	if (*(uint16_t *)((char *)pad + 240))
		return 0;
	return just_down(pad, 4) || just_down(pad, 7) || (pad[2] >= 64 && pad[23] < 64);
}

/* NvFEOF and OS_FileRead read the end-of-file flag straight out of the FILE
 * as bionic lays it out (flags at +12, __SEOF 0x20); the FILE is glibc's
 * here, so a short last read never counted as the end of the file but as
 * an error, and CFileMgr::ReadLine dropped the file's last partial
 * 255-byte chunk: the last lines of the map .ide files went missing (in
 * Vice City a mission object the script needs, a crash on New Game). On
 * the phone the files come out of the OBB zip, another path. */
static int NvFEOF(const uintptr_t *f)
{
	return f[0] == 1 ? feof((FILE *)f[1]) != 0 : 1;
}

/* A save is written to GTA3sf<n>.b.tmp and then renamed through Java
 * (FileRename): done here, relative to game/ (the working directory) */
static int AND_FileRename(const char *from, const char *to, int overwrite)
{
	if (!overwrite && access(to, F_OK) == 0)
		return 0;
	if (rename(from, to) != 0) {
		log_printf("game: rename %s -> %s failed\n", from, to);
		return 0;
	}
	return 1;
}

/* videos (the logos and the opening titles, MainGameTick's first states):
 * on Android a Java MediaPlayer shows them over the game, which meanwhile
 * ticks without drawing or swapping and polls OS_MovieIsPlaying. movie.c
 * plays game/movies/. The poll runs on the game thread, which here doesn't
 * hold the screen: GraphicsThread does, idling between empty render queues,
 * and its sleeps show the newest picture (w_usleep in imports.c,
 * movie_present: the player's own GL context). A or Start
 * skips. OS_MovieStop is a 4-byte branch to AND_StopMovie, too short for a
 * hook, so AND_StopMovie is hooked. */
static SOFTFP int OS_MoviePlay(const char *file, int a2, int a3, float a4)
{
	movie_play(file);
	return 0;
}

static int AND_IsMoviePlaying(void) { return movie_playing(); }
static void AND_StopMovie(void) { movie_stop(); }

void patch_game(void)
{
	hook_sym("_Z15OS_ThreadLaunchPFjPvES_jPKci16OSThreadPriority", OS_ThreadLaunch);
	hook_sym("_Z17OS_ScreenGetWidthv", OS_ScreenGetWidth);
	hook_sym("_Z18OS_ScreenGetHeightv", OS_ScreenGetHeight);
	hook_sym("_Z20AND_SystemInitializev", ret0);
	hook_sym("_Z13ProcessEventsb", game_ProcessEvents);

	/* no APK: the files are loose in game/, and NvFOpen only falls back to
	 * the APK (a Java helper) when fopen fails */
	hook_sym("_Z9NvAPKInitv", ret0);
	hook_sym("_Z9NvAPKOpenPKc", ret0);

	hook_sym("_Z14AND_FileRenamePKcS0_i", AND_FileRename);

	/* Invert Look: CPad::LookAroundUpDown already turns the stick by the
	 * setting, and CCamera::CamControl turned it again by the same
	 * setting before feeding the free camera, so the two cancelled and
	 * the setting did nothing: CamControl's factor stays -1 (its
	 * "it ne; vmovne.f32 s12, s14", 1.0 when inverted, goes); off then
	 * pushes the view the way the stick goes */
	static const uint8_t it_vmovne[6] = { 0x18, 0xbf, 0xb0, 0xee, 0x47, 0x6a }, nops[6] = { 0x00, 0xbf, 0x00, 0xbf, 0x00, 0xbf };
	if (memcmp((void *)(so_base + 0x130BA0), it_vmovne, 6) == 0)
		so_patch(0x130BA0, nops, 6);
	else
		log_printf("game: invert look patch not applied\n");

	{
		the_camera = (char *)so_find_addr("TheCamera");
		CPlayerPed__DoesTargetHaveToBeBroken = (void *)so_find_addr("_ZN10CPlayerPed24DoesTargetHaveToBeBrokenE7CVectorP7CWeapon");
		CGeneral__GetATanOfXY = (void *)so_find_addr("_ZN8CGeneral11GetATanOfXYEff");
		CSprite__CalcScreenCoors = (void *)so_find_addr("_ZN7CSprite15CalcScreenCoorsERK5RwV3dPS0_PfS4_b");
		CWorld__GetIsLineOfSightClear = (void *)so_find_addr("_ZN6CWorld21GetIsLineOfSightClearERK7CVectorS2_bbbbbbb");
		hook_sym("_ZN10CPlayerPed14EvaluateTargetEP7CEntityPS1_Pfffbb", evaluate_target);
		hook_sym("_ZN10CPlayerPed26EvaluateNeighbouringTargetEP7CEntityPS1_Pfffbbb", evaluate_neighbouring_target);
		static const struct { uintptr_t off; uint8_t orig[4]; } sees[] = {
			{ 0x1BD6A6, { 0xce, 0xf7, 0x91, 0xfd } }, { 0x1BD894, { 0xce, 0xf7, 0x9a, 0xfc } },
		};
		for (size_t i = 0; i < sizeof(sees) / sizeof(*sees); i++)
			if (!patch_bl(sees[i].off, sees[i].orig, (uintptr_t)return_1))
				log_printf("game: lock-on pre-check at %#x not patched\n", (unsigned)sees[i].off);
	}

	hook_sym("_ZN4CPad23ShiftTargetLeftJustDownEv", shift_target_left);
	hook_sym("_ZN4CPad24ShiftTargetRightJustDownEv", shift_target_right);

	{
		static const struct { uintptr_t off; uint8_t orig[4]; int limited; } refresh[] = {
			{ 0x20BC1E, { 0xea, 0xf0, 0x37, 0xf8 }, 0 }, /* limiter off */
			{ 0x20BCC6, { 0xe9, 0xf0, 0xe3, 0xff }, 0 }, /* menu */
			{ 0x20BDD6, { 0xe9, 0xf0, 0x5b, 0xff }, 1 }, /* on, 60 */
			{ 0x20BE26, { 0xe9, 0xf0, 0x33, 0xff }, 1 }, /* on, 30 */
		};
		for (size_t i = 0; i < sizeof(refresh) / sizeof(*refresh); i++)
			if (!patch_bl(refresh[i].off, refresh[i].orig, refresh[i].limited ? (uintptr_t)refresh_limited : (uintptr_t)refresh_free))
				log_printf("game: frame limiter call at %#x not patched\n", (unsigned)refresh[i].off);
	}

	hook_sym("_Z6NvFEOFPv", NvFEOF);
	/* OS_FileRead's own copy of the check, on a raw FILE: a short read is
	 * the end of the file (ldrh r3, [r3, #12] -> b to its return 2) */
	static const uint8_t ldrh_flags[2] = { 0x9b, 0x89 }, b_eof[2] = { 0xf8, 0xe7 };
	if (memcmp((void *)(so_base + 0x2FD4DE), ldrh_flags, 2) == 0)
		so_patch(0x2FD4DE, b_eof, 2);
	else
		log_printf("game: OS_FileRead end-of-file patch not applied\n");

	{
		touchscreen = (void **)so_find_addr("GTouchscreen");
		Touchscreen__GetRadarButton = (void *)so_find_addr("_ZN11Touchscreen14GetRadarButtonEv");
		Touchscreen__GetSummaryButton = (void *)so_find_addr("_ZN11Touchscreen16GetSummaryButtonEv");
		Touchscreen__GetTutButton = (void *)so_find_addr("_ZN11Touchscreen12GetTutButtonEv");
	}

	/* no app-rating popup, no vibration feedback (IsCloudAvailable and
	 * NewCloudSaveAvailable already return 0 in this build; at 4 bytes
	 * they are too short to hook: the hooks overwrote each other and
	 * saving, which asks right after writing, jumped into garbage) */
	hook_sym("_Z15CheckForAppRatev", ret0);
	*(uint8_t *)so_find_addr("UseTouchSense") = 0;

	hook_sym("_Z12OS_MoviePlayPKcbbf", OS_MoviePlay);
	hook_sym("_Z18AND_IsMoviePlayingv", AND_IsMoviePlaying);
	hook_sym("_Z13AND_StopMoviev", AND_StopMovie);

	/* straight to the menu: the legal screens count as shown */
	*(uint8_t *)so_find_addr("shownLegalScreen") = 1;

	hook_sym("_ZN24CControllerConfigManager26GetWideStringOfCommandKeysEtPtt", GetWideStringOfCommandKeys);

	CText__Load = (void *)trampoline("_ZN5CText4LoadEv");
	CText__LoadMissionText = (void *)trampoline("_ZN5CText15LoadMissionTextEPc");
	hook_sym("_ZN5CText4LoadEv", CText__Load_hook);
	hook_sym("_ZN5CText15LoadMissionTextEPc", CText__LoadMissionText_hook);

	hook_sym("_Z25WarGamepad_GetGamepadTypev", WarGamepad_GetGamepadType);
	hook_sym("_Z28WarGamepad_GetGamepadButtonsv", WarGamepad_GetGamepadButtons);
	hook_sym("_Z25WarGamepad_GetGamepadAxisi", WarGamepad_GetGamepadAxis);
}
