#include <raylib.h>
#include <stdio.h>
#include <stddef.h>
#include <unistd.h>

#define RAYGUI_IMPLEMENTATION
#include <raygui.h>

#define RAYGUI_VERT_SLIDE_IMPLEMENTATION
#include <raygui_vert_slider.h>

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

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
#define RING_VOLUME_OFFSET (-0.25) /* FIXME: Changing this doesn't seem to effect the wave at all... */

#define FUEL_RESTORE_RATE 50 /* Liters/s */
#define MAX_FUEL_LEVEL 100000.0 /* Liters */
#define MAX_COOLER_TEMP 1400 /* Degrees C */
#define COOLER_COOL_RATE(x) ((powf((x / MAX_COOLER_TEMP),2) * (MAX_COOLER_TEMP * 0.2)) /* Degrees/s */
#define MAX_INPUT_POWER 2980 /* Amps */
#define FUEL_CONSUME_RATE(x) ((-1 * (powf(x*20, 3))) + FUEL_RESTORE_RATE)

typedef struct power_drain_s
{
    float rate; /* in watts */
    float rand; /* rename this - random chance of power draw/peak? */
} power_drain_t;

typedef struct capacitor_s
{

    /* Capacitors have:
	- sizes (small, medium, large)
	- Quality (low, average, high)

	- frequency tolerance?

	smaller capacitors can't hold as much charge, so they are required to have power drawn from them in order to not over-charge, over-heat, and incur damage

    */
} capacitor_t;

typedef struct power_tap_s
{
    float	    level; /* This is the instantaneous level of input power based on overall power output */
    capacitor_t	    cap; /* The amount of power available for the drain is stored in the capacitor */
    power_drain_t   dest;
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
static float cooler_temp;
static float fuel_level;
static float fuel_rate;
static float quantum_level;
static float total_output_power;
static bool engine_overload;
static bool update_waveforms;

static power_tap_t tap_bat;
static power_tap_t tap_1;
static power_tap_t tap_2;
static power_tap_t tap_3;
static power_drain_t drain_battery;
static power_drain_t drain_shields;
static power_drain_t drain_weapons;
static power_drain_t drain_thrust;






void set_root_freq(float freq)
{
    ma_waveform_set_frequency(&waveforms.rootwave, freq);
}

void set_root_power(float power)
{
    ma_waveform_set_amplitude(&waveforms.rootwave, (double)power);
}

void set_q_freq(float freq)
{
    ma_waveform_set_frequency(&waveforms.qwave, freq);
}

void set_q_power(float power)
{
    ma_waveform_set_amplitude(&waveforms.qwave, (double)power);
}

void set_r_freq(float freq)
{
    ma_waveform_set_frequency(&waveforms.rwave, freq);
}

void set_r_power(float power)
{
    ma_waveform_set_amplitude(&waveforms.rwave, (double)power);
}

void set_s_freq(float freq)
{
    ma_waveform_set_frequency(&waveforms.swave, freq);
}

void set_s_power(float power)
{
    ma_waveform_set_amplitude(&waveforms.swave, (double)power);
}

void draw_gui(void)
{
    float	gui_value;
    bool	input_power_changed = false;

    BeginDrawing();
    ClearBackground(BLACK);

    /* ============== TITLE ============== */
    DrawText("SC Pulse Engine POC Demo", (WIN_WIDTH >> 1) - 140, 5, 20, LIGHTGRAY);

    /* ================ Cooler Capacity ================= */
    GuiProgressBar((Rectangle){115, 30, 760, 24}, "Cooler temp", TextFormat("%0.2f", cooler_temp), &cooler_temp, 0.0, MAX_COOLER_TEMP);

    /* ============== Fuel Capacity ============== */
    /* <capacity bar> <capacity label (float in liters)> <consumption rate (liters/sec)> */
    GuiProgressBar((Rectangle){115, 70, 760, 24}, "Fuel", TextFormat("%6.0f", fuel_level), &fuel_level, 0.0, MAX_FUEL_LEVEL);
    GuiLabel((Rectangle){950, 70, 70, 24}, TextFormat("%2.2f L/s", fuel_rate));

    /* ================== Input Power ================ */
    /* FIXME: Make conversion functions that convert the percentages and raw frequencies to the display values */
    GuiGroupBox((Rectangle){ 20, 120, 200, 280 }, "Input Power");
    gui_value = GuiVerticalSliderBar((Rectangle){ 40, 150, 34, 192 }, "Amps", TextFormat("%4.0f", waveforms.rootwave_vol * 1675), waveforms.rootwave_vol, 0.0f, 1.0f);
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
    GuiGroupBox((Rectangle){ 240, 120, 200, 280 }, "Q-Ring");
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
    GuiGroupBox((Rectangle){ 460, 120, 200, 280 }, "R-Ring");
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
    GuiGroupBox((Rectangle){ 680, 120, 200, 280 }, "S-Ring");
    gui_value = GuiVerticalSlider((Rectangle){ 700, 150, 34, 192 }, "Freq", TextFormat("%2.2f", waveforms.swave_freq), waveforms.swave_freq, ROOT_FREQ - S_VARIANCE, ROOT_FREQ + S_VARIANCE);
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


    /* ============= Total Power Output  =============== */
    int c = GuiGetStyle(PROGRESSBAR, BASE_COLOR_PRESSED);
    bool ovrld = engine_overload; /* Since updates are in another thread, only check once */
    if (ovrld)
	GuiSetStyle(PROGRESSBAR, BASE_COLOR_PRESSED, 0xff2020ff);
    GuiProgressBar((Rectangle){115, 420, 760, 24}, "Power Output", TextFormat("%0.2f", total_output_power), &total_output_power, 0.0, 1.0);
    if (ovrld)
	GuiSetStyle(PROGRESSBAR, BASE_COLOR_PRESSED, c);


    /* =========== Power Taps =========== */
    GuiProgressBar((Rectangle){115, 450, 100, 10}, "Power Taps:", NULL, &tap_bat.level, 0.0, 1.0);
    GuiProgressBar((Rectangle){235, 450, 200, 10}, NULL, NULL, &tap_1.level, 0.0, 1.0);
    GuiProgressBar((Rectangle){455, 450, 200, 10}, NULL, NULL, &tap_2.level, 0.0, 1.0);
    GuiProgressBar((Rectangle){675, 450, 200, 10}, NULL, NULL, &tap_3.level, 0.0, 1.0);



    EndDrawing();
}

void damage_engine(void)
{
    /* Update damage counter bar and add a hefty bump to heat output */
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
    float	    tap_battery;
    float	    tap_1;
    float	    tap_2;
    float	    tap_3;

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
	    damage_engine();
	}
	else
	    engine_overload = false;

	total_output_power = max_signal;
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
}

int main(int argc, char *argv[])
{
    ma_context context;
    ma_device_config dev_config;
    ma_device device;

    ma_engine_config engine_config;
    ma_engine engine;

    ma_sound snd_clank_s;
    ma_sound snd_clank_m;
    ma_sound snd_clank_l;

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

    ma_device_start(&device);


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
    fuel_level = MAX_FUEL_LEVEL;

    GuiLoadStyle(GUI_THEME_RGS);
    while (!WindowShouldClose())
    {
	float frame_time;


	draw_gui();

	update_fuel();
	update_power_taps();

	{
	}

	usleep(16666);
    }
    CloseWindow();

    ma_device_stop(&device);
    ma_device_uninit(&device);

    ma_engine_uninit(&engine);
    return 0;
}



