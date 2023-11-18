/* The following is based on Raylib's raygui/examples/custom_sliders/custom_sliders.c
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
#ifndef RAYGUI_VERT_SLIDE_H
#define RAYGUI_VERT_SLIDE_H

/* This requires that raygui.h has already been included in the module that uses is */

float GuiVerticalSlider(Rectangle bounds, const char *textTop, const char *textBottom, float value, float minValue, float maxValue);
float GuiVerticalSliderBar(Rectangle bounds, const char *textTop, const char *textBottom, float value, float minValue, float maxValue);
float GuiVerticalSliderPro(Rectangle bounds, const char *textTop, const char *textBottom, float value, float minValue, float maxValue, int sliderHeight);

#endif /* RAYGUI_VERT_SLIDE_H */

#if defined(RAYGUI_VERT_SLIDE_IMPLEMENTATION)

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
#endif /* RAYGUI_VERT_SLIDE_IMPLEMENTATION */
