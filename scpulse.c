#include <raylib.h>
#include <stdio.h>
#include <stddef.h>
#include <unistd.h>

#define RAYGUI_IMPLEMENTATION
#include <raygui.h>

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

#define FUEL_RESTORE_RATE 8 /* Liters/s */
#define MAX_FUEL_LEVEL 100000.0 /* Liters */
#define MAX_COOLER_TEMP 1400 /* Degrees C */
#define COOLER_COOL_RATE(x) ((powf((x / MAX_COOLER_TEMP),2) * (MAX_COOLER_TEMP * 0.2)) /* Degrees/s */
#define MAX_INPUT_POWER 2980 /* Amps */
#define FUEL_CONSUME_RATE(x) ((-1 * (powf(x*10, 3))) + FUEL_RESTORE_RATE)

/* FIXME: Make sure threaded accesses are safe. Use volatile... */
static float cooler_temp;
static float fuel_level;
static float fuel_rate;
static float quantum_level;
static float total_output_power;
static bool engine_overload;
static bool update_waveforms;


float GuiVerticalSlider(Rectangle bounds, const char *textTop, const char *textBottom, float value, float minValue, float maxValue);
float GuiVerticalSliderBar(Rectangle bounds, const char *textTop, const char *textBottom, float value, float minValue, float maxValue);
float GuiVerticalSliderPro(Rectangle bounds, const char *textTop, const char *textBottom, float value, float minValue, float maxValue, int sliderHeight);

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

static sine_sources_t waveforms;

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

    //total_output_power = 0;


    EndDrawing();
}

void update_engine_damage(void)
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
	    update_engine_damage();
	}
	else
	    engine_overload = false;

	total_output_power = max_signal;
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
	usleep(16666);
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
    CloseWindow();

    ma_device_stop(&device);
    ma_device_uninit(&device);

    ma_engine_uninit(&engine);
    return 0;
}


/* The following is copied from Raylib's raygui/examples/custom_sliders/custom_sliders.c
   It is licenced as follows:
zlib License
	
Copyright (c) 2014-2023 Ramon Santamaria (@raysan5)

This software is provided "as-is", without any express or implied warranty. In no event 
will the authors be held liable for any damages arising from the use of this software.

Permission is granted to anyone to use this software for any purpose, including commercial 
applications, and to alter it and redistribute it freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not claim that you 
  wrote the original software. If you use this software in a product, an acknowledgment 
  in the product documentation would be appreciated but is not required.

  2. Altered source versions must be plainly marked as such, and must not be misrepresented
  as being the original software.

  3. This notice may not be removed or altered from any source distribution.
*/
float GuiVerticalSlider(Rectangle bounds, const char *textTop, const char *textBottom, float value, float minValue, float maxValue)
{
    return GuiVerticalSliderPro(bounds, textTop, textBottom, value, minValue, maxValue, GuiGetStyle(SLIDER, SLIDER_WIDTH));
}

float GuiVerticalSliderBar(Rectangle bounds, const char *textTop, const char *textBottom, float value, float minValue, float maxValue)
{
    return GuiVerticalSliderPro(bounds, textTop, textBottom, value, minValue, maxValue, 0);
}

float GuiVerticalSliderPro(Rectangle bounds, const char *textTop, const char *textBottom, float value, float minValue, float maxValue, int sliderHeight)
{
    GuiState state = (GuiState)GuiGetState();

    int sliderValue = (int)(((value - minValue)/(maxValue - minValue)) * (bounds.height - 2 * GuiGetStyle(SLIDER, BORDER_WIDTH)));

    Rectangle slider = {
        bounds.x + GuiGetStyle(SLIDER, BORDER_WIDTH) + GuiGetStyle(SLIDER, SLIDER_PADDING),
        bounds.y + bounds.height - sliderValue,
        bounds.width - 2*GuiGetStyle(SLIDER, BORDER_WIDTH) - 2*GuiGetStyle(SLIDER, SLIDER_PADDING),
        0.0f,
    };

    if (sliderHeight > 0)        // Slider
    {
        slider.y -= sliderHeight/2;
        slider.height = (float)sliderHeight;
    }
    else if (sliderHeight == 0)  // SliderBar
    {
        slider.y -= GuiGetStyle(SLIDER, BORDER_WIDTH);
        slider.height = (float)sliderValue;
    }
    // Update control
    //--------------------------------------------------------------------
    if ((state != STATE_DISABLED) && !guiLocked)
    {
        Vector2 mousePoint = GetMousePosition();

        if (CheckCollisionPointRec(mousePoint, bounds))
        {
            if (IsMouseButtonDown(MOUSE_LEFT_BUTTON))
            {
                state = STATE_PRESSED;

                // Get equivalent value and slider position from mousePoint.x
                float normalizedValue = (bounds.y + bounds.height - mousePoint.y - (float)(sliderHeight / 2)) / (bounds.height - (float)sliderHeight);
                value = (maxValue - minValue) * normalizedValue + minValue;

                if (sliderHeight > 0) slider.y = mousePoint.y - slider.height / 2;  // Slider
                else if (sliderHeight == 0)                                          // SliderBar
                {
                    slider.y = mousePoint.y;
                    slider.height = bounds.y + bounds.height - slider.y - GuiGetStyle(SLIDER, BORDER_WIDTH);
                }
            }
            else state = STATE_FOCUSED;
        }

        if (value > maxValue) value = maxValue;
        else if (value < minValue) value = minValue;
    }


    // Bar limits check
    if (sliderHeight > 0)        // Slider
    {
        if (slider.y < (bounds.y + GuiGetStyle(SLIDER, BORDER_WIDTH))) slider.y = bounds.y + GuiGetStyle(SLIDER, BORDER_WIDTH);
        else if ((slider.y + slider.height) >= (bounds.y + bounds.height)) slider.y = bounds.y + bounds.height - slider.height - GuiGetStyle(SLIDER, BORDER_WIDTH);
    }
    else if (sliderHeight == 0)  // SliderBar
    {
        if (slider.y < (bounds.y + GuiGetStyle(SLIDER, BORDER_WIDTH)))
        {
            slider.y = bounds.y + GuiGetStyle(SLIDER, BORDER_WIDTH);
            slider.height = bounds.height - 2*GuiGetStyle(SLIDER, BORDER_WIDTH);
        }
    }

    //--------------------------------------------------------------------
    // Draw control
    //--------------------------------------------------------------------
    GuiDrawRectangle(bounds, GuiGetStyle(SLIDER, BORDER_WIDTH), Fade(GetColor(GuiGetStyle(SLIDER, BORDER + (state*3))), guiAlpha), Fade(GetColor(GuiGetStyle(SLIDER, (state != STATE_DISABLED)?  BASE_COLOR_NORMAL : BASE_COLOR_DISABLED)), guiAlpha));

    // Draw slider internal bar (depends on state)
    if ((state == STATE_NORMAL) || (state == STATE_PRESSED)) GuiDrawRectangle(slider, 0, BLANK, Fade(GetColor(GuiGetStyle(SLIDER, BASE_COLOR_PRESSED)), guiAlpha));
    else if (state == STATE_FOCUSED) GuiDrawRectangle(slider, 0, BLANK, Fade(GetColor(GuiGetStyle(SLIDER, TEXT_COLOR_FOCUSED)), guiAlpha));

    // Draw top/bottom text if provided
    if (textTop != NULL)
    {
        Rectangle textBounds = { 0 };
        textBounds.width = (float)GetTextWidth(textTop);
        textBounds.height = (float)GuiGetStyle(DEFAULT, TEXT_SIZE);
        textBounds.x = bounds.x + bounds.width/2 - textBounds.width/2;
        textBounds.y = bounds.y - textBounds.height - GuiGetStyle(SLIDER, TEXT_PADDING);

        GuiDrawText(textTop, textBounds, TEXT_ALIGN_RIGHT, Fade(GetColor(GuiGetStyle(SLIDER, TEXT + (state*3))), guiAlpha));
    }

    if (textBottom != NULL)
    {
        Rectangle textBounds = { 0 };
        textBounds.width = (float)GetTextWidth(textBottom);
        textBounds.height = (float)GuiGetStyle(DEFAULT, TEXT_SIZE);
        textBounds.x = bounds.x + bounds.width/2 - textBounds.width/2;
        textBounds.y = bounds.y + bounds.height + GuiGetStyle(SLIDER, TEXT_PADDING);

        GuiDrawText(textBottom, textBounds, TEXT_ALIGN_LEFT, Fade(GetColor(GuiGetStyle(SLIDER, TEXT + (state*3))), guiAlpha));
    }
    //--------------------------------------------------------------------

    return value;
}

