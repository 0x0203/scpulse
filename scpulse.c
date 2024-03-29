#ifdef __EMSCRIPTEN__
#include <raylib_web.h>
#else
#include <raylib.h>
#endif
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <unistd.h>

#define RAYGUI_IMPLEMENTATION
#include <raygui.h>

#define RAYGUI_VERT_SLIDE_IMPLEMENTATION
#include <raygui_vert_slider.h>

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#ifdef __EMSCRIPTEN__
#include <style_cyber.h>
#endif

//#define MY_SAMPLE_RATE 48000
#define MY_SAMPLE_RATE 44100

#define WIN_WIDTH 1024
#define WIN_HEIGHT 768

#define ROOT_FREQ   40.0
#define Q_VARIANCE  2.7
#define R_VARIANCE  0.81
#define S_VARIANCE  0.53

#define GUI_THEME_RGS "resources/style_cyber.rgs"

#define DEFAULT_VOLUME 0.25;

#define FUEL_RESTORE_RATE 50 /* Liters/s */
#define MAX_FUEL_LEVEL 100000.0 /* Liters */
#define MAX_COOLER_TEMP 1400 /* Degrees C */
#define COOLER_COOL_RATE(x) ((powf((x / MAX_COOLER_TEMP), 1.2) * (MAX_COOLER_TEMP * 0.01))) /* Degrees/s */
#define MAX_INPUT_POWER 2980 /* Amps */
#define FUEL_CONSUME_RATE(x) ((-1 * (powf(x*20, 3))) + FUEL_RESTORE_RATE)
#define POWER_TO_TEMP(x) (powf(x*33, 2)) /* 0 < x < 1.0 */

#define HEALTH_TO_COLOR(h) (0x000000ff | (0x10 << 24) | ((127 + ((uint8_t)(h * 100))) << 16) | (0x65 << 8) )

#define CHARGE_TO_COLOR(c, full, max)																    \
	    (0x000000ff |																	    \
	    (c > full ? (c >= (max - 0.006) ? (0xff << 24) : (((uint8_t)(((1.0 - (max - c) / (max - full))) * 0x5f) + 0xa0) << 24)) : (0x40 << 24) )   |	    \
	    (c > full ? (c >= (max - 0.006) ? (0x10 << 16) : (((uint8_t)(((1.0 - (max - c) / (max - full))) * 0x40) + 0x10) << 16)) : (0x33 << 16) )   |	    \
	    (c > full ? (c >= (max - 0.006) ? (0x10 << 8)  : (((uint8_t)((0.0 + ((max - c) / (max - full))) * 0xc0) + 0x30) << 8))  : (0xc9 << 8)  ) )

#define TEMP_TO_COLOR(temp)	((0x000000ff) | ((0xff & (uint8_t)(((temp) / MAX_COOLER_TEMP) * 255)) << 24) | (0x20 << 0x10) | (((temp) >= MAX_COOLER_TEMP ? 0x30 : 0x80) << 8))

#define POWER_TAP_DEST_STRING "Thrusters;Shields;Weapons"
typedef enum {
    TAP_DEST_THRUST = 0,
    TAP_DEST_SHIELD = 1,
    TAP_DEST_WEAPON = 2,
} tap_dest_e;

#define CAPACITOR_SIZE_STRING "Small (10)\nMedium (20)\nLarge (50)"
typedef enum {
    CAP_SIZE_SMALL = 0,
    CAP_SIZE_MED = 1,
    CAP_SIZE_LARGE = 2,
} capacitor_size_e;

#define CAPACITOR_GRADE_STRING "Consumer\nProfessional\nMilitary"
typedef enum {
    CAP_GRADE_CON = 0,
    CAP_GRADE_PRO = 1,
    CAP_GRADE_MIL = 2,
} capacitor_grade_e;


#define MAX_BAT_CHARGE 100.0

static const float cap_max_charges[] = {15.0, 30.0, 70.0}; /* Indexed by capacitor_size_e */
static const float cap_full_limits[] = {5.0, 10.0, 20.0}; /* cap_max_charges - max_full is "full" */
static const float cap_grade_chrg_rate[] = {1.0, 2.0, 3.0}; /* Indexed by capacitor_grade_e */
static const float cap_grade_dmg_factor[] = {3.0, 2.0, 1.0}; /* Indexed by capacitor_grade_e */

/* TODO: Add otehr tables for heat tolerance, damage multipliers, etc... */

typedef struct capacitor_s
{
    /* Capacitors have:
	- sizes (small, medium, large)
	- Quality (low, average, high)

	- frequency tolerance?

	smaller capacitors can't hold as much charge, so they are required to have power drawn from them in order to not over-charge, over-heat, and incur damage

    */
    float		health;
    float		charge;
    float		max_charge;
    float		full_limit;


    capacitor_grade_e	grade;
    capacitor_size_e	size;
} capacitor_t;

typedef struct power_drain_s
{
    float   rate;
    float   spike_probability; /* random chance of power draw/peak */
    bool    enabled;
    float   factor; /* multiplier for how quickly the source gets drained */
} power_drain_t;

typedef struct power_tap_s
{
    float	    level; /* This is the instantaneous level of input power based on overall power output. Range: 0 - 1.0 */
    capacitor_t	    cap; /* The amount of power available for the drain is stored in the capacitor */
    power_drain_t   *drain;

    float	    charge_mult;

    /* Gui data */
    tap_dest_e selected_dest;
    bool edit_mode;

} power_tap_t;

typedef struct sine_sources_s
{
    ma_waveform_config rootwave_cfg;
    ma_waveform rootwave;
    float rootbuf[MY_SAMPLE_RATE];

    ma_waveform_config qwave_cfg;
    ma_waveform qwave;
    float qbuf[MY_SAMPLE_RATE];

    ma_waveform_config rwave_cfg;
    ma_waveform rwave;
    float rbuf[MY_SAMPLE_RATE];

    ma_waveform_config swave_cfg;
    ma_waveform swave;
    float sbuf[MY_SAMPLE_RATE];

    float rootwave_vol;
    float qwave_vol;
    float rwave_vol;
    float swave_vol;

    float rootwave_freq;
    float qwave_freq;
    float rwave_freq;
    float swave_freq;
} sine_sources_t;

/* FIXME: Make sure threaded accesses are safe. Use volatile... */
static sine_sources_t waveforms;
volatile float cooler_temp;
static float fuel_level;
static float fuel_rate;
//static float quantum_level;
static float total_output_power;
static float engine_health;
static bool engine_overload;

static power_tap_t tap_bat;
static power_tap_t tap_1;
static power_tap_t tap_2;
static power_tap_t tap_3;
static power_drain_t drain_shields;
static power_drain_t drain_weapons;
static power_drain_t drain_thrust;

static bool quitting;

/* Function declarations */
static void cooler_add_heat(float d);






static void capacitor_reset(capacitor_t *cap)
{
    cap->charge = 0.0;
    cap->health = 1.0;
    cap->max_charge = cap_max_charges[(int)cap->size];

}

#if 0
static void set_root_freq(float freq)
{
    ma_waveform_set_frequency(&waveforms.rootwave, freq);
}
#endif

static void set_root_power(float power)
{
    ma_waveform_set_amplitude(&waveforms.rootwave, (double)power);
}

static void set_q_freq(float freq)
{
    ma_waveform_set_frequency(&waveforms.qwave, freq);
}

static void set_q_power(float power)
{
    ma_waveform_set_amplitude(&waveforms.qwave, (double)power);
}

static void set_r_freq(float freq)
{
    ma_waveform_set_frequency(&waveforms.rwave, freq);
}

static void set_r_power(float power)
{
    ma_waveform_set_amplitude(&waveforms.rwave, (double)power);
}

static void set_s_freq(float freq)
{
    ma_waveform_set_frequency(&waveforms.swave, freq);
}

static void set_s_power(float power)
{
    ma_waveform_set_amplitude(&waveforms.swave, (double)power);
}

static void randomize_drains(void)
{
    drain_thrust.spike_probability = GetRandomValue(1, 60) / 1000.0;
    drain_shields.spike_probability = GetRandomValue(1, 180) / 1000.0;
    drain_weapons.spike_probability = GetRandomValue(1, 100) / 1000.0;
}

void draw_gui(void)
{
    float	gui_value;
    float	f;
    int		c;
    bool	input_power_changed = false;
    capacitor_grade_e last_grade;
    capacitor_size_e	last_size;

    BeginDrawing();
    ClearBackground(BLACK);

    /* ============== TITLE ============== */
    DrawText("SC Pulse Engine PoC Demo", (WIN_WIDTH >> 1) - 140, 5, 20, LIGHTGRAY);

    /* ================ Cooler Capacity ================= */
    c = GuiGetStyle(PROGRESSBAR, BASE_COLOR_PRESSED);
    GuiSetStyle(PROGRESSBAR, BASE_COLOR_PRESSED, TEMP_TO_COLOR(cooler_temp >= MAX_COOLER_TEMP ? MAX_COOLER_TEMP : cooler_temp));
    GuiProgressBar((Rectangle){115, 30, 760, 24}, "Cooler temp", TextFormat("%0.2f", cooler_temp > MAX_COOLER_TEMP ? MAX_COOLER_TEMP : cooler_temp), (float *)&cooler_temp, 0.0, MAX_COOLER_TEMP);
    GuiSetStyle(PROGRESSBAR, BASE_COLOR_PRESSED, c);

    /* ============== Fuel Capacity ============== */
    /* <capacity bar> <capacity label (float in liters)> <consumption rate (liters/sec)> */
    if (GuiButton((Rectangle){20, 70, 55, 24}, "Refuel"))
    {
	fuel_level = MAX_FUEL_LEVEL;
    }
    GuiProgressBar((Rectangle){115, 70, 760, 24}, "Fuel", TextFormat("%6.0f", fuel_level), &fuel_level, 0.0, MAX_FUEL_LEVEL);
    GuiLabel((Rectangle){950, 70, 70, 24}, TextFormat("%2.2f L/s", fuel_level >= MAX_FUEL_LEVEL ? 0.0 : fuel_rate));

    /* ================== Input Power ================ */
    GuiGroupBox((Rectangle){ 120, 120, 100, 255 }, "Input Power");
    gui_value = GuiVerticalSliderBar((Rectangle){ 155, 150, 34, 192 }, "Amps", TextFormat("%4.0f", waveforms.rootwave_vol * 1675), waveforms.rootwave_vol, 0.0f, 1.0f);
    if (gui_value != waveforms.rootwave_vol)
    {
	if (fuel_level > 0)
	{
	    input_power_changed = true;
	    waveforms.rootwave_vol = gui_value;
	    set_root_power(waveforms.rootwave_vol);
	}
    }

    /* ============= Q-Ring Settings =============== */
    GuiGroupBox((Rectangle){ 240, 120, 200, 255 }, "Q-Ring");
    gui_value = GuiVerticalSlider((Rectangle){ 260, 150, 34, 192 }, "Freq", TextFormat("%2.2f", waveforms.qwave_freq), waveforms.qwave_freq, ROOT_FREQ + 0.07, ROOT_FREQ + Q_VARIANCE);
    if (gui_value != waveforms.qwave_freq)
    {
	waveforms.qwave_freq = gui_value;
	set_q_freq(waveforms.qwave_freq);
    }
    gui_value = GuiVerticalSliderBar((Rectangle){ 320, 150, 34, 192 }, "Power", TextFormat("%0.2f", waveforms.qwave_vol), waveforms.qwave_vol, 0.0f, 1.0f);
    if (input_power_changed || gui_value != waveforms.qwave_vol)
    {
	waveforms.qwave_vol = gui_value;
	set_q_power(waveforms.qwave_vol * waveforms.rootwave_vol);
    }

    /* ============= R-Ring Settings =============== */
    GuiGroupBox((Rectangle){ 460, 120, 200, 255 }, "R-Ring");
    gui_value = GuiVerticalSlider((Rectangle){ 480, 150, 34, 192 }, "Freq", TextFormat("%2.2f", waveforms.rwave_freq), waveforms.rwave_freq, ROOT_FREQ - R_VARIANCE, ROOT_FREQ - 0.11);
    if (gui_value != waveforms.rwave_freq)
    {
	waveforms.rwave_freq = gui_value;
	set_r_freq(waveforms.rwave_freq);
    }
    gui_value = GuiVerticalSliderBar((Rectangle){ 540, 150, 34, 192 }, "Power", TextFormat("%0.2f", waveforms.rwave_vol), waveforms.rwave_vol, 0.0f, 1.0f);
    if (input_power_changed || gui_value != waveforms.rwave_vol)
    {
	waveforms.rwave_vol = gui_value;
	set_r_power(waveforms.rwave_vol * waveforms.rootwave_vol);
    }

    /* ============= S-Ring Settings =============== */
    GuiGroupBox((Rectangle){ 680, 120, 200, 255 }, "S-Ring");
    gui_value = GuiVerticalSlider((Rectangle){ 700, 150, 34, 192 }, "Freq", TextFormat("%2.2f", waveforms.swave_freq), waveforms.swave_freq, ROOT_FREQ - S_VARIANCE, ROOT_FREQ + S_VARIANCE);
    if (gui_value >= ROOT_FREQ && gui_value < ROOT_FREQ + 0.02) gui_value = ROOT_FREQ + 0.02;
    if (gui_value > ROOT_FREQ - 0.02  && gui_value < ROOT_FREQ) gui_value = ROOT_FREQ - 0.02;
    if (gui_value != waveforms.swave_freq)
    {
	waveforms.swave_freq = gui_value;
	set_s_freq(waveforms.swave_freq);
    }
    gui_value = GuiVerticalSliderBar((Rectangle){ 760, 150, 34, 192 }, "Power", TextFormat("%0.2f", waveforms.swave_vol), waveforms.swave_vol, 0.0f, 1.0f);
    if (input_power_changed || gui_value != waveforms.swave_vol)
    {
	waveforms.swave_vol = gui_value;
	set_s_power(waveforms.swave_vol * waveforms.rootwave_vol);
    }


    /* ============== Engine Health ============== */
    c = GuiGetStyle(PROGRESSBAR, BASE_COLOR_PRESSED);
    GuiSetStyle(PROGRESSBAR, BASE_COLOR_PRESSED, HEALTH_TO_COLOR(engine_health));
    GuiProgressBar((Rectangle){115, 390, 760, 15}, "Engine Health", TextFormat("%0.2f", engine_health), &engine_health, 0.0, 1.0);
    GuiSetStyle(PROGRESSBAR, BASE_COLOR_PRESSED, c);
    if (GuiButton((Rectangle){925, 390, 85, 15}, "Repair"))
	engine_health = 1.0;



    /* ============= Total Power Output  =============== */
    c = GuiGetStyle(PROGRESSBAR, BASE_COLOR_PRESSED);
    bool ovrld = engine_overload; /* Since updates are in another thread, only check once */
    if (ovrld)
	GuiSetStyle(PROGRESSBAR, BASE_COLOR_PRESSED, 0xff2020ff);
    GuiProgressBar((Rectangle){115, 418, 760, 24}, "Power Output", TextFormat("%0.2f", total_output_power), &total_output_power, 0.0, 1.0);
    if (ovrld)
	GuiSetStyle(PROGRESSBAR, BASE_COLOR_PRESSED, c);


    /* ============== Battery ================ */
    GuiSetState(STATE_DISABLED);
    GuiVerticalSliderBar((Rectangle){135, 500, 60, 200}, "Charge", TextFormat("%0.1f", tap_bat.cap.charge), tap_bat.cap.charge, 0.0f, MAX_BAT_CHARGE);
    GuiSetState(STATE_NORMAL);



    /* ============= Capacitor 1 ============= */
    GuiGroupBox((Rectangle){235, 490, 200, 220}, "Capacitor 1");

    if (tap_1.edit_mode) GuiLock();

    GuiLabel((Rectangle){275, 500, 80, 16}, "Size");
    last_size = tap_1.cap.size;
    GuiToggleGroup((Rectangle){245, 520, 80, 25}, CAPACITOR_SIZE_STRING, (int *)&tap_1.cap.size);
    tap_1.cap.max_charge = cap_max_charges[(int)tap_1.cap.size];
    tap_1.cap.full_limit = tap_1.cap.max_charge - cap_full_limits[(int)tap_1.cap.size];

    GuiLabel((Rectangle){365, 500, 80, 16}, "Grade");
    last_grade = tap_1.cap.grade;
    GuiToggleGroup((Rectangle){345, 520, 80, 25}, CAPACITOR_GRADE_STRING, (int *)&tap_1.cap.grade);

    if (last_size != tap_1.cap.size || last_grade != tap_1.cap.grade)
	capacitor_reset(&tap_1.cap);

    if (tap_1.edit_mode) GuiUnlock();

    c = GuiGetStyle(PROGRESSBAR, BASE_COLOR_PRESSED);
    GuiSetStyle(PROGRESSBAR, BASE_COLOR_PRESSED, HEALTH_TO_COLOR(tap_1.cap.health));
    GuiProgressBar((Rectangle){285, 620, 110, 20}, "Health", TextFormat("%2.2f", tap_1.cap.health), &tap_1.cap.health, 0.0, 1.0);
    f = tap_1.cap.charge;
    GuiSetStyle(PROGRESSBAR, BASE_COLOR_PRESSED, CHARGE_TO_COLOR(tap_1.cap.charge, tap_1.cap.full_limit,  tap_1.cap.max_charge));
    GuiProgressBar((Rectangle){285, 650, 110, 30}, "Charge", TextFormat("%2.2f", f > tap_1.cap.full_limit ? tap_1.cap.full_limit : f),
		    &f, 0.0, tap_1.cap.full_limit);
    GuiSetStyle(PROGRESSBAR, BASE_COLOR_PRESSED, c);



    /* ============= Capacitor 2 ============= */
    GuiGroupBox((Rectangle){455, 490, 200, 220}, "Capacitor 2");

    if (tap_2.edit_mode) GuiLock();

    GuiLabel((Rectangle){495, 500, 80, 16}, "Size");
    last_size = tap_2.cap.size;
    GuiToggleGroup((Rectangle){475, 520, 80, 25}, CAPACITOR_SIZE_STRING, (int *)&tap_2.cap.size);
    tap_2.cap.max_charge = cap_max_charges[(int)tap_2.cap.size];
    tap_2.cap.full_limit = tap_2.cap.max_charge - cap_full_limits[(int)tap_2.cap.size];

    GuiLabel((Rectangle){585, 500, 80, 16}, "Grade");
    last_grade = tap_2.cap.grade;
    GuiToggleGroup((Rectangle){565, 520, 80, 25}, CAPACITOR_GRADE_STRING, (int *)&tap_2.cap.grade);

    if (last_size != tap_2.cap.size || last_grade != tap_2.cap.grade)
	capacitor_reset(&tap_2.cap);

    if (tap_2.edit_mode) GuiUnlock();

    c = GuiGetStyle(PROGRESSBAR, BASE_COLOR_PRESSED);
    GuiSetStyle(PROGRESSBAR, BASE_COLOR_PRESSED, HEALTH_TO_COLOR(tap_2.cap.health));
    GuiProgressBar((Rectangle){505, 620, 110, 20}, "Health", TextFormat("%2.2f", tap_2.cap.health), &tap_2.cap.health, 0.0, 1.0);
    f = tap_2.cap.charge;
    GuiSetStyle(PROGRESSBAR, BASE_COLOR_PRESSED, CHARGE_TO_COLOR(tap_2.cap.charge, tap_2.cap.full_limit,  tap_2.cap.max_charge));
    GuiProgressBar((Rectangle){505, 650, 110, 30}, "Charge", TextFormat("%2.2f", f > tap_2.cap.full_limit ? tap_2.cap.full_limit : f),
		    &f, 0.0, tap_2.cap.full_limit);
    GuiSetStyle(PROGRESSBAR, BASE_COLOR_PRESSED, c);



    /* ============= Capacitor 3 ============= */
    GuiGroupBox((Rectangle){675, 490, 200, 220}, "Capacitor 3");

    if (tap_3.edit_mode) GuiLock();

    GuiLabel((Rectangle){715, 500, 80, 16}, "Size");
    last_size = tap_3.cap.size;
    GuiToggleGroup((Rectangle){695, 520, 80, 25}, CAPACITOR_SIZE_STRING, (int *)&tap_3.cap.size);
    tap_3.cap.max_charge = cap_max_charges[(int)tap_3.cap.size];
    tap_3.cap.full_limit = tap_3.cap.max_charge - cap_full_limits[(int)tap_3.cap.size];

    GuiLabel((Rectangle){805, 500, 80, 16}, "Grade");
    last_grade = tap_3.cap.grade;
    GuiToggleGroup((Rectangle){785, 520, 80, 25}, CAPACITOR_GRADE_STRING, (int *)&tap_3.cap.grade);

    if (last_size != tap_3.cap.size || last_grade != tap_3.cap.grade)
	capacitor_reset(&tap_3.cap);

    if (tap_3.edit_mode) GuiUnlock();

    c = GuiGetStyle(PROGRESSBAR, BASE_COLOR_PRESSED);
    GuiSetStyle(PROGRESSBAR, BASE_COLOR_PRESSED, HEALTH_TO_COLOR(tap_3.cap.health));
    GuiProgressBar((Rectangle){725, 620, 110, 20}, "Health", TextFormat("%2.2f", tap_3.cap.health), &tap_3.cap.health, 0.0, 1.0);
    f = tap_3.cap.charge;
    GuiSetStyle(PROGRESSBAR, BASE_COLOR_PRESSED, CHARGE_TO_COLOR(tap_3.cap.charge,  tap_3.cap.full_limit, tap_3.cap.max_charge));
    GuiProgressBar((Rectangle){725, 650, 110, 30}, "Charge", TextFormat("%2.2f", f > tap_3.cap.full_limit ? tap_3.cap.full_limit : f),
		    &f, 0.0, tap_3.cap.full_limit);
    GuiSetStyle(PROGRESSBAR, BASE_COLOR_PRESSED, c);



    /* =========== Power Taps =========== */
    /* Dropdowns need to be drawn after anything they might cover, so do these last */
    GuiProgressBar((Rectangle){115, 450, 100, 10}, "Power Taps:", NULL, &tap_bat.level, 0.0, 1.0);
    GuiProgressBar((Rectangle){235, 450, 200, 10}, NULL, NULL, &tap_1.level, 0.0, 1.0);
    GuiProgressBar((Rectangle){455, 450, 200, 10}, NULL, NULL, &tap_2.level, 0.0, 1.0);
    GuiProgressBar((Rectangle){675, 450, 200, 10}, NULL, NULL, &tap_3.level, 0.0, 1.0);

    GuiLabel((Rectangle){50, 465, 80, 15}, "Routed to:");
    GuiLabel((Rectangle){115, 465, 100, 15}, "      Battery");
    if (GuiDropdownBox((Rectangle){235, 465, 200, 15}, POWER_TAP_DEST_STRING, (int *)&tap_1.selected_dest, tap_1.edit_mode))
	tap_1.edit_mode = !tap_1.edit_mode;
    if (GuiDropdownBox((Rectangle){455, 465, 200, 15}, POWER_TAP_DEST_STRING, (int *)&tap_2.selected_dest, tap_2.edit_mode))
	tap_2.edit_mode = !tap_2.edit_mode;
    if (GuiDropdownBox((Rectangle){675, 465, 200, 15}, POWER_TAP_DEST_STRING, (int *)&tap_3.selected_dest, tap_3.edit_mode))
	tap_3.edit_mode = !tap_3.edit_mode;



    /* =========== Power Drains ========== */
    GuiLabel((Rectangle){125, WIN_HEIGHT -15, 85, 10}, "Power Usage:");
    GuiProgressBar((Rectangle){295, WIN_HEIGHT - 15, 115, 10}, "Thrusters", NULL, &drain_thrust.rate, 0.0, 1.0);
    GuiProgressBar((Rectangle){505, WIN_HEIGHT - 15, 115, 10}, "Shields", NULL, &drain_shields.rate, 0.0, 1.0);
    GuiProgressBar((Rectangle){735, WIN_HEIGHT - 15, 115, 10}, "Weapons", NULL, &drain_weapons.rate, 0.0, 1.0);
    GuiCheckBox((Rectangle){415, WIN_HEIGHT - 18, 15, 15}, NULL, &drain_thrust.enabled);
    GuiCheckBox((Rectangle){625, WIN_HEIGHT - 18, 15, 15}, NULL, &drain_shields.enabled);
    GuiCheckBox((Rectangle){855, WIN_HEIGHT - 18, 15, 15}, NULL, &drain_weapons.enabled);

    if (GuiButton((Rectangle){900, WIN_HEIGHT - 18, 80, 16}, "Randomize"))
    {
	randomize_drains();
    }



    EndDrawing();
}

void audio_damage_engine(void)
{
    /* This is called from the audio processing thread, which means it happens thousands of times per second.
     * It is not suitable for use in the main thread.
     */

    /* Update damage counter bar and add a hefty bump to heat output */
    engine_health -= 0.000002;
    if (engine_health < 0)
	engine_health = 0;

    cooler_add_heat(0.01);
}

/* Sound rendering function. Sound wave is combined, examined, normalized, and sent to sound card here */
void data_callback(ma_device *pDevice, void *pOutput, const void *pInput, ma_uint32 frameCount)
{
    /* This functions makes the assumption that we're dealing with only a single channel. It will
     * break if that is ever not the case.
     */
    sine_sources_t  *srcs;
    float	    *output;
    int		    i;
    float	    max_signal = 0;

    srcs = (sine_sources_t *)pDevice->pUserData;
    output = (float *)pOutput;

    ma_waveform_read_pcm_frames(&srcs->rootwave, srcs->rootbuf, frameCount, NULL);
    ma_waveform_read_pcm_frames(&srcs->qwave, srcs->qbuf, frameCount, NULL);
    ma_waveform_read_pcm_frames(&srcs->rwave, srcs->rbuf, frameCount, NULL);
    ma_waveform_read_pcm_frames(&srcs->swave, srcs->sbuf, frameCount, NULL);

    for (i=0; i < frameCount; i++)
    {
	float t = srcs->rootbuf[i] + srcs->qbuf[i] + srcs->rbuf[i] + srcs->sbuf[i];
	output[i] = t;

	t = fabsf(t);
	if (t > max_signal)
	    max_signal = t;

	if (max_signal > 1.0)
	{
	    engine_overload = true;
	    audio_damage_engine();
	}
	else
	    engine_overload = false;

	total_output_power = max_signal;
    }
}

static void update_engine(void)
{
    if (engine_health <= 0)
    {
	waveforms.rootwave_vol = 0.0;
	set_root_power(0.0);
	set_q_power(waveforms.qwave_vol * waveforms.rootwave_vol);
	set_r_power(waveforms.rwave_vol * waveforms.rootwave_vol);
	set_s_power(waveforms.swave_vol * waveforms.rootwave_vol);
    }

}

static void damage_engine(float f)
{
    engine_health -= f;
    if (engine_health < 0)
	engine_health = 0;
}

static void cooler_add_heat(float d)
{
    cooler_temp += d;
}

static void cooler_dissipate_heat(void)
{
    cooler_temp -= COOLER_COOL_RATE(cooler_temp);

    if (cooler_temp < 0)
	cooler_temp = 0;
}

static void update_engine_heat(void)
{
    float f;
    f = waveforms.rootwave_vol;

    f += POWER_TO_TEMP(waveforms.qwave_vol) * 0.4;
    f += POWER_TO_TEMP(waveforms.rwave_vol) * 0.3;
    f += POWER_TO_TEMP(waveforms.swave_vol) * 0.2;

    f *= waveforms.rootwave_vol;
    cooler_add_heat(f/12);
    cooler_dissipate_heat();
    if (cooler_temp > MAX_COOLER_TEMP)
    {
	    damage_engine(0.0001 * (cooler_temp - MAX_COOLER_TEMP));
    }
}

static void update_fuel(void)
{
    float frame_time;

    frame_time = GetFrameTime();
    fuel_rate = FUEL_CONSUME_RATE(waveforms.rootwave_vol);
    fuel_level += frame_time * fuel_rate;
    if (fuel_level < 0)
    {
	fuel_level = 0.0;

	waveforms.rootwave_vol = 0.0;
	set_root_power(0.0);

	set_q_power(waveforms.qwave_vol * waveforms.rootwave_vol);
	set_r_power(waveforms.rwave_vol * waveforms.rootwave_vol);
	set_s_power(waveforms.swave_vol * waveforms.rootwave_vol);
    }
    else if (fuel_level > MAX_FUEL_LEVEL)
	fuel_level = MAX_FUEL_LEVEL;
}

static power_drain_t *tap_sel_to_drain(tap_dest_e d)
{
    switch(d)
    {
    case TAP_DEST_THRUST:
	return &drain_thrust;
    case TAP_DEST_SHIELD:
	return &drain_shields;
    case TAP_DEST_WEAPON:
	return &drain_weapons;
    default:
	printf("bad item selection\n");
	quitting = true;
    }
    return NULL;
}

static void update_drains(void)
{
    /* Thrusters get a pretty sinusoidal power usage, with a low degree of variance. */
    static float thrust_freq;
    if (thrust_freq == 0.0)
	thrust_freq = 1.0 * (GetRandomValue(1, 100) / 100.0);

    if (drain_thrust.enabled)
    {
	drain_thrust.rate = (sin(GetTime() * thrust_freq) + 1) / 2.0;
	if ((GetRandomValue(1, 100) / 100.0) <= drain_thrust.spike_probability)
	{
	    thrust_freq = 1.0 * (GetRandomValue(1, 100) / 100.0);
	}
    }
    else
	drain_thrust.rate = 0;


    /* Shields get large spikes that gradually drain away */
    if (drain_shields.enabled)
    {
	if (GetRandomValue(1, 100) / 100.0 <= drain_shields.spike_probability)
	{
	    drain_shields.rate += (GetRandomValue(1, 30) / 100.0);
	}
	drain_shields.rate -= drain_shields.rate * 0.1;
	if (drain_shields.rate > 1.0) drain_shields.rate = 1.0;
	if (drain_shields.rate < 0) drain_shields.rate = 0;
    }
    else
	drain_shields.rate = 0;


    /* Weapons gradually, quickly, build up, then drop to nothing */
    if (drain_weapons.enabled)
    {
	static int weapons_charging;

	if (weapons_charging || GetRandomValue(1,100) / 100.0 <= drain_weapons.spike_probability)
	{
	    weapons_charging++;
	}
	if (weapons_charging > 60 || GetRandomValue(1,100)/100.0 < drain_weapons.spike_probability)
	{
	    weapons_charging = 0;
	    drain_weapons.rate -= 0.2;
	}

	if (weapons_charging)
	    drain_weapons.rate += 0.01 + (drain_weapons.rate * 0.1);
	else
	    drain_weapons.rate -= 0.2;

	if (drain_weapons.rate > 1.0) drain_weapons.rate = 1.0;
	if (drain_weapons.rate < 0) drain_weapons.rate = 0;
    }
    else
	drain_weapons.rate = 0;

}

static void update_power_taps(void)
{
    /* Bottom 10% goes to battery, remaining 90% divided evenly in 3*/
    if (total_output_power <= 0.1)
    {
	tap_bat.level = total_output_power / 0.1;
	tap_1.level = 0.0;
	tap_2.level = 0.0;
	tap_3.level = 0.0;
    }
    else if (total_output_power <= 0.4)
    {
	tap_bat.level = 1.0;
	tap_1.level = (total_output_power - 0.1) / 0.3;
	tap_2.level = 0.0;
	tap_3.level = 0.0;
    }
    else if (total_output_power <= .7)
    {
	tap_bat.level = 1.0;
	tap_1.level = 1.0;
	tap_2.level = (total_output_power - 0.4) / 0.3;
	tap_3.level = 0.0;
    }
    else if (total_output_power <= 1.0)
    {
	tap_bat.level = 1.0;
	tap_1.level = 1.0;
	tap_2.level = 1.0;
	tap_3.level = (total_output_power - 0.7) / 0.3;
    }
    else
    {
	tap_bat.level = 1.0;
	tap_1.level = 1.0;
	tap_2.level = 1.0;
	tap_3.level = 1.0;
    }


    tap_1.drain = tap_sel_to_drain(tap_1.selected_dest);
    tap_2.drain = tap_sel_to_drain(tap_2.selected_dest);
    tap_3.drain = tap_sel_to_drain(tap_3.selected_dest);

    return;
}

static void fill_capacitor(power_tap_t *tap, float strength)
{
    float f = tap->level * cap_grade_dmg_factor[(int)tap->cap.grade];

    if (tap->cap.charge > tap->cap.full_limit)
	strength *= (2 * cap_grade_dmg_factor[(int)tap->cap.grade]);

    if (tap->cap.health > 0)
    {
	tap->cap.charge +=  strength *
			    (tap->level / cap_max_charges[(int)tap->cap.size]) *
			    cap_grade_chrg_rate[(int)tap->cap.grade] *
			    tap->cap.health;
    }

    if (tap->cap.charge > tap->cap.max_charge)
    {
	tap->cap.health -= 0.0001 * tap->level * f;
	cooler_add_heat(1.0 * f);
	if (tap->cap.health < 0)
	    tap->cap.health = 0;

	if (tap->cap.health == 0)
	    tap->cap.charge = 0;
	else
	    tap->cap.charge = tap->cap.max_charge;
    }
    else
	cooler_add_heat(0.1 * (tap->cap.charge / tap->cap.max_charge) * f);
}

static void drain_battery(power_drain_t *d, float pct)
{
    tap_bat.cap.charge -= (d->rate * d->factor * pct);
    cooler_add_heat(d->rate * 4.3 * pct);
    if (tap_bat.cap.charge < 0)
    {
	d->enabled = 0;
	tap_bat.cap.charge = 0;
    }
}

static void drain_capacitor(power_tap_t *tap)
{
    /* This assumes that fill_capacitor has been called this frame */

    power_drain_t   *d = tap->drain;
    float	    rate;
    int		    num_connected = 0; /* The number of drains that this tap shares */

    if (tap->cap.charge > tap->cap.full_limit)
    {
	/* Based on grade, the capacitor charge will decay until it reaches its full limit. Higher grade capacitors will discharge more slowly. The less
	 * charge currently feeding this tap, the more quickly it will will discharge.
	 */
	float decay = 0.1;

	decay *= cap_grade_dmg_factor[(int)tap->cap.grade];
	if (tap->level <= 0)
	    tap->cap.charge -= decay;
	//decay *= 1.0 - tap->level;
	//tap->cap.charge -= decay;
	if (tap->cap.charge < tap->cap.full_limit)
	    tap->cap.charge = tap->cap.full_limit;

    }

    if (tap_1.drain == tap->drain && tap_1.cap.charge > 0) num_connected++;
    if (tap_2.drain == tap->drain && tap_2.cap.charge > 0) num_connected++;
    if (tap_3.drain == tap->drain && tap_3.cap.charge > 0) num_connected++;

    if (num_connected == 0)
    {
	/* Drain battery instead */
	drain_battery(tap->drain, 1.0);
    }
    else
    {
	float diff;
	rate = d->rate / num_connected;
	rate *= d->factor;
	if (rate > tap->cap.charge)
	{
	    diff = rate - tap->cap.charge;
	    drain_battery(tap->drain, diff/rate);
	}
	tap->cap.charge -= rate;
	if (tap->cap.charge < 0)
	    tap->cap.charge = 0;


    }

}
/* TODO: Implement power delivered progress bars below Power Usage (change to Power Requested) to show how much power is actually getting to the
 *	 thrusters/shields/weapons. Make it so that power delivered is less if it's coming from batteries. */
/* TODO: Add a route to battery option for the power taps */
/* TODO: Tune and balance */

static void update_capacitors(void)
{
    drain_capacitor(&tap_1);
    drain_capacitor(&tap_2);
    drain_capacitor(&tap_3);

    if (tap_1.drain != &drain_shields && tap_2.drain != &drain_shields && tap_3.drain != &drain_shields)
    {
	drain_battery(&drain_shields, 1.0);
    }
    if (tap_1.drain != &drain_weapons && tap_2.drain != &drain_weapons && tap_3.drain != &drain_weapons)
    {
	drain_battery(&drain_weapons, 1.0);
    }
    if (tap_1.drain != &drain_thrust && tap_2.drain != &drain_thrust && tap_3.drain != &drain_thrust)
    {
	drain_battery(&drain_thrust, 1.0);
    }

    fill_capacitor(&tap_1, 0.1);
    fill_capacitor(&tap_2, 0.2);
    fill_capacitor(&tap_3, 0.6);

}

static void update_battery(void)
{
    tap_bat.cap.charge += 0.03 * (tap_bat.level / MAX_BAT_CHARGE);
    if (tap_bat.cap.charge > MAX_BAT_CHARGE)
	tap_bat.cap.charge = MAX_BAT_CHARGE;
}

void main_loop__em()
{

	draw_gui();


	update_engine();
	update_fuel();
	update_drains();
	update_power_taps();
	update_battery();
	update_capacitors();
	update_engine_heat();

}

int main(int argc, char *argv[])
{
    ma_context context;
    ma_device_config dev_config;
    ma_device device;

    ma_engine_config engine_config;
    ma_engine engine;

#if 0
    ma_sound snd_clank_s;
    ma_sound snd_clank_m;
    ma_sound snd_clank_l;
#endif

    ma_result res;

    InitWindow(WIN_WIDTH, WIN_HEIGHT, "SCPulseEngine");

    waveforms.rootwave_vol = 0;
    waveforms.qwave_vol = 0;
    waveforms.rwave_vol = 0;
    waveforms.swave_vol = 0;
    waveforms.rootwave_freq = ROOT_FREQ;
    waveforms.qwave_freq = ROOT_FREQ;
    waveforms.rwave_freq = ROOT_FREQ;
    waveforms.swave_freq = ROOT_FREQ;

    res = ma_context_init(NULL, 0, NULL, &context);
    if (res != MA_SUCCESS)
    {
	fprintf(stderr, "Failed to initialize context - %s\n", ma_result_description(res));
	return -1;
    }

    engine_config = ma_engine_config_init();
    res = ma_engine_init(&engine_config, &engine);
    if (res != MA_SUCCESS)
    {
	fprintf(stderr, "Failed to init sound engine - %s\n", ma_result_description(res));
	return -1;
    }

    dev_config			    = ma_device_config_init(ma_device_type_playback);
    dev_config.playback.format	    = ma_format_f32;
    dev_config.playback.channels    = 1;
    dev_config.sampleRate	    = MY_SAMPLE_RATE;
    dev_config.dataCallback	    = data_callback;
    dev_config.pUserData	    = &waveforms; /* Gets set to device.pUserData */

    res = ma_device_init(&context, &dev_config, &device);
    if (res != MA_SUCCESS)
    {
	fprintf(stderr, "Failed to initialize the device - %s\n", ma_result_description(res));
	return -1;
    }



    waveforms.rootwave_cfg = ma_waveform_config_init(device.playback.format, device.playback.channels,
						     device.sampleRate, ma_waveform_type_sine,
						     waveforms.rootwave_vol, waveforms.rootwave_freq);
    waveforms.qwave_cfg = ma_waveform_config_init(device.playback.format, device.playback.channels,
						     device.sampleRate, ma_waveform_type_sine,
						     waveforms.qwave_vol, waveforms.qwave_freq);
    waveforms.rwave_cfg = ma_waveform_config_init(device.playback.format, device.playback.channels,
						     device.sampleRate, ma_waveform_type_sine,
						     waveforms.rwave_vol, waveforms.rwave_freq);
    waveforms.swave_cfg = ma_waveform_config_init(device.playback.format, device.playback.channels,
						     device.sampleRate, ma_waveform_type_sine,
						     waveforms.swave_vol, waveforms.swave_freq);

    ma_waveform_init(&waveforms.rootwave_cfg, &waveforms.rootwave);
    ma_waveform_init(&waveforms.qwave_cfg, &waveforms.qwave);
    ma_waveform_init(&waveforms.rwave_cfg, &waveforms.rwave);
    ma_waveform_init(&waveforms.swave_cfg, &waveforms.swave);

    ma_device_start(&device);


#if 0
    res = ma_sound_init_from_file(&engine, "resources/clank_s.wav",
				  MA_SOUND_FLAG_DECODE |
				  MA_SOUND_FLAG_NO_PITCH, NULL, NULL, &snd_clank_s);
    if (res != MA_SUCCESS)
    {
	fprintf(stderr, "Failed to initialize sound file resources/clanc_s.wav - %s\n", ma_result_description(res));
	return -1;
    }
    res = ma_sound_init_from_file(&engine, "resources/clank_m.wav",
				  MA_SOUND_FLAG_DECODE |
				  MA_SOUND_FLAG_NO_PITCH, NULL, NULL, &snd_clank_m);
    if (res != MA_SUCCESS)
    {
	fprintf(stderr, "Failed to initialize sound file resources/clanc_m.wav - %s\n", ma_result_description(res));
	return -1;
    }
    res = ma_sound_init_from_file(&engine, "resources/clank_l.wav",
				  MA_SOUND_FLAG_DECODE |
				  MA_SOUND_FLAG_NO_PITCH, NULL, NULL, &snd_clank_l);
    if (res != MA_SUCCESS)
    {
	fprintf(stderr, "Failed to initialize sound file resources/clanc_l.wav - %s\n", ma_result_description(res));
	return -1;
    }

    ma_sound_start(&snd_clank_s);
    ma_sound_start(&snd_clank_m);
    ma_sound_start(&snd_clank_l);

    ma_sound_uninit(&snd_clank_l);
    ma_sound_uninit(&snd_clank_m);
    ma_sound_uninit(&snd_clank_s);
#endif

    /* Init "Game" elements */
    engine_health = 1.0;
    fuel_level = MAX_FUEL_LEVEL;
    tap_1.selected_dest = TAP_DEST_THRUST;
    tap_1.drain = tap_sel_to_drain(tap_1.selected_dest);
    tap_1.cap.size = CAP_SIZE_SMALL;
    tap_1.cap.grade = CAP_GRADE_CON;
    capacitor_reset(&tap_1.cap);

    tap_2.selected_dest = TAP_DEST_SHIELD;
    tap_2.drain = tap_sel_to_drain(tap_2.selected_dest);
    tap_2.cap.size = CAP_SIZE_SMALL;
    tap_2.cap.grade = CAP_GRADE_CON;
    capacitor_reset(&tap_2.cap);

    tap_3.selected_dest = TAP_DEST_WEAPON;
    tap_3.drain = tap_sel_to_drain(tap_3.selected_dest);
    tap_3.cap.size = CAP_SIZE_SMALL;
    tap_3.cap.grade = CAP_GRADE_CON;
    capacitor_reset(&tap_3.cap);

    tap_bat.cap.charge = 50.0;

    drain_shields.rate = 0.5;
    drain_weapons.rate = 0.5;
    drain_thrust.rate = 0.5;

    drain_shields.factor = 0.04;
    drain_weapons.factor = 0.01;
    drain_thrust.factor = 0.0078;

    drain_shields.enabled = false;
    drain_weapons.enabled = false;
    drain_thrust.enabled = false;
    randomize_drains();


#ifdef __EMSCRIPTEN__
    GuiLoadStyleCyber();
    emscripten_set_main_loop(main_loop__em, 0, 1);
#else

    GuiLoadStyle(GUI_THEME_RGS);

    while (!WindowShouldClose() && !quitting)
    {
	float frame_time;
	main_loop__em();

	usleep(16666);
    }
#endif

    CloseWindow();

    ma_device_stop(&device);
    ma_device_uninit(&device);

    ma_engine_uninit(&engine);
    return 0;
}



