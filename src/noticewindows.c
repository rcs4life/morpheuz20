/*
 * Morpheuz Sleep Monitor
 *
 * Copyright (c) 2013-2015 James Fowler
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "pebble.h"
#include "morpheuz.h"
#include "language.h"
  
#define MOON_START GRect(width+6, 72, 58, 46)  
#ifdef PBL_ROUND
  #define MOON_FINISH GRect(centre - 29, 5, 58, 46)
#else
  #define MOON_FINISH GRect(6, 5, 58, 46)
#endif

static AppTimer *notice_timer;

static BitmapLayerComp notice_moon;

extern bool menu_live;
extern GFont notice_font;

#ifndef PBL_ROUND
static TextLayer *notice_name_layer;
#endif
  
static TextLayer *notice_text;

static Window *notice_window;

static bool notice_showing = false;

static struct PropertyAnimation *moon_animation;

static char *buffer;


  
/*
 * Remove the notice window
 */
EXTFN void hide_notice_layer(void *data) {
  if (notice_showing) {
    window_stack_remove(notice_window, true);
    macro_bitmap_layer_destroy(&notice_moon);
    #ifndef PBL_ROUND
      text_layer_destroy(notice_name_layer);
    #endif
    text_layer_destroy(notice_text);
    window_destroy(notice_window);
    free(buffer);
    notice_showing = false;
  }
}

/*
 * End of notice window animation
 */
static void moon_animation_stopped(Animation *animation, bool finished, void *data) {
#ifdef PBL_SDK_2
  animation_unschedule(animation);
  animation_destroy(animation);
#endif
}

static void load_resource_into_buffer(uint32_t resource_id) {
  ResHandle rh = resource_get_handle(resource_id);
  size_t size = resource_size(rh);
  if (size > (BUFFER_SIZE - 1)) {
    size = (BUFFER_SIZE - 1);
  }
  memset(buffer, '\0', BUFFER_SIZE);
  resource_load(rh, (uint8_t *) buffer, size);
  text_layer_set_text(notice_text, buffer);
}

/*
 * Click a button on a notice and it goes away
 */
static void single_click_handler(ClickRecognizerRef recognizer, void *context) {
    if (notice_showing) {
      app_timer_reschedule(notice_timer, SHORT_RETRY_MS);
    }
}

/*
 * Button config
 */
static void notice_click_config_provider(Window *window) {
  window_single_click_subscribe(BUTTON_ID_BACK, single_click_handler);
  window_single_click_subscribe(BUTTON_ID_UP, single_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, single_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, single_click_handler);
}

/*
 * Show the notice window
 */
EXTFN void show_notice(uint32_t resource_id) {
  
  // If the menu is showing then it is rude to interup
  if (menu_live) {
    return;
  }

  // It's night - want to see message
  light_enable_interaction();

  // Already showing - change message
  if (notice_showing) {
    load_resource_into_buffer(resource_id);
    app_timer_reschedule(notice_timer, NOTICE_DISPLAY_MS);
    return;
  }

  buffer = malloc(BUFFER_SIZE);

  // Bring up notice
  notice_showing = true;
  notice_window = window_create();
#ifdef PBL_SDK_2
  window_set_fullscreen(notice_window, true);
#endif
  window_stack_push(notice_window, true /* Animated */);

  bool invert = get_config_data()->invert;
  GColor fcolor = invert ? GColorBlack : GColorWhite;

  window_set_background_color(notice_window, invert ? GColorWhite : BACKGROUND_COLOR);

  Layer *window_layer = window_get_root_layer(notice_window);
  
  GRect bounds = layer_get_bounds(window_layer);
  int16_t centre = bounds.size.w / 2;
  int16_t width = bounds.size.w;

  macro_bitmap_layer_create(&notice_moon, MOON_START, window_layer, invert ? RESOURCE_ID_KEYBOARD_BG_WHITE : RESOURCE_ID_KEYBOARD_BG, true);

  #ifndef PBL_ROUND
  notice_name_layer = macro_text_layer_create(GRect(5, 15, 134, 30), window_layer, fcolor, GColorClear, notice_font, GTextAlignmentRight);
  text_layer_set_text(notice_name_layer, MORPHEUZ);
  #endif

  notice_text = macro_text_layer_create(GRect(0, 68, width, 100), window_layer, fcolor, GColorClear, notice_font, GTextAlignmentCenter);
  load_resource_into_buffer(resource_id);

  window_set_click_config_provider(notice_window, (ClickConfigProvider) notice_click_config_provider);

  moon_animation = property_animation_create_layer_frame(bitmap_layer_get_layer_jf(notice_moon.layer), &MOON_START, &MOON_FINISH);
  animation_set_duration((Animation*) moon_animation, 750);
  animation_set_handlers((Animation*) moon_animation, (AnimationHandlers ) { .stopped = (AnimationStoppedHandler) moon_animation_stopped, }, NULL /* callback data */);

  animation_schedule((Animation*) moon_animation);

  notice_timer = app_timer_register(NOTICE_DISPLAY_MS, hide_notice_layer, NULL);
}

EXTFN bool is_notice_showing() {
  return notice_showing;
}

