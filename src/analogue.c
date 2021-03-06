/*
 * Morpheuz Sleep Monitor
 *
 * Copyright (c) 2013-2016 James Fowler
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
#include "analogue.h"
#include "morpheuz.h"
#include "rootui.h"
  
#ifdef PBL_RECT

#define WIDTH_SMART_POINTS 3
#define WIDTH_HOUR_MARKS 3
#define WIDTH_MINUTES PBL_IF_COLOR_ELSE(2,1)

const GPathInfo MINUTE_HAND_POINTS = HAND_MACRO(49);

const GPathInfo HOUR_HAND_POINTS = HAND_MACRO(35);

static Layer *analogue_layer;

static GPath *minute_arrow;
static GPath *hour_arrow;
static Layer *hands_layer;
static struct PropertyAnimation* analogue_animation;

static bool show_smart_points;
static int16_t from_time;
static int16_t to_time;
static int16_t start_time;
static int16_t start_time_round;
static int16_t progress_1;
static int16_t progress_2;
static bool is_visible = false;
static bool g_call_post_init;

/*
 * Draws marks around the circumference of the clock face
 * inner = pixels innermost
 * outer = pixels outermost
 * start = 0 to 1440 starting position
 * stop = 0 to 1440 ending position
 * step = 120: hourly; 24: minute; etc
 * width = line thickening 
 * color = line color
 */
static void draw_marks(Layer *layer, GContext *ctx, int inner, int outer, int start, int stop, int step, int width, GColor color) {
  graphics_context_set_stroke_color(ctx, color);
  
  GRect bounds = layer_get_bounds(layer);
  const GPoint center = grect_center_point(&bounds);
  const int16_t furthestOut = bounds.size.w / 2;
  const int16_t pixelsInner = furthestOut - inner;
  const int16_t pixelsOuter = furthestOut - outer;

  GPoint pointInner;
  GPoint pointOuter;
  
  graphics_context_set_stroke_width(ctx, width);
  for (int i = start; i < stop; i += step) {
      int32_t second_angle = (TRIG_MAX_ANGLE * i / 1440);
      int32_t minus_cos = -cos_lookup(second_angle);
      int32_t plus_sin = sin_lookup(second_angle);

      pointInner.y = (int16_t) (minus_cos * (int32_t) pixelsInner / TRIG_MAX_RATIO) + center.y;
      pointInner.x = (int16_t) (plus_sin * (int32_t) pixelsInner / TRIG_MAX_RATIO) + center.x;
      pointOuter.y = (int16_t) (minus_cos * (int32_t) pixelsOuter / TRIG_MAX_RATIO) + center.y;
      pointOuter.x = (int16_t) (plus_sin * (int32_t) pixelsOuter / TRIG_MAX_RATIO) + center.x;

      graphics_draw_line(ctx, pointInner, pointOuter);
  }
}

/*
 * Update the clockface layer if it needs it
 */
static void bg_update_proc(Layer *layer, GContext *ctx) {

  graphics_context_set_fill_color(ctx, BACKGROUND_COLOR);
  graphics_fill_rect(ctx, layer_get_bounds(layer), 0, GCornerNone);

  graphics_context_set_fill_color(ctx, ANALOGUE_COLOR);

  #ifdef PBL_COLOR
    graphics_context_set_compositing_mode(ctx, GCompOpSet);
  #endif

  // Start and first and last times for smart alarm
  if (show_smart_points) {
    draw_marks(layer, ctx, OUTER_STOP, OUTER, from_time, from_time + 1, 1, WIDTH_SMART_POINTS, FROM_TIME_COLOR);
    draw_marks(layer, ctx, OUTER_STOP, OUTER, to_time, to_time + 1, 1, WIDTH_SMART_POINTS, TO_TIME_COLOR);
  }

  // Minute marks
  draw_marks(layer, ctx, MIN, CLOCK, 0, 1440, MINUTE_STEP, WIDTH_MINUTES, MINUTE_MARK_COLOR);

  // Show reset point
  if (start_time != -1) {
    draw_marks(layer, ctx, OUTER_STOP, OUTER, start_time, start_time + 1, 1, WIDTH_SMART_POINTS, START_TIME_COLOR);

    // Progress line
    if (progress_1 != -1) {
      draw_marks(layer, ctx, MIN, CLOCK, start_time_round, progress_1, PROGRESS_STEP, WIDTH_MINUTES, PROGRESS_COLOR);
      if (progress_2 != -1) {
        draw_marks(layer, ctx, MIN, CLOCK, 0, progress_2, PROGRESS_STEP, WIDTH_MINUTES, PROGRESS_COLOR);
      }
    }
  }
  
  // Hour marks
  draw_marks(layer, ctx, HOUR, CLOCK, 0, 1440, 120, WIDTH_HOUR_MARKS, HOUR_MARK_COLOR);

}

/*
 * Record the smart times for display on the analogue clock. Trigger an update of the layer.
 */
EXTFN void analogue_set_smart_times() {
  show_smart_points = get_config_data()->smart;
  from_time = (get_config_data()->from * 2) % 1440;
  to_time = (get_config_data()->to * 2) % 1440;
  if (is_visible)
    layer_mark_dirty(analogue_layer);
}

/*
 * Record the base time for display on the analogue clock. Trigger an update of the layer.
 */
EXTFN void analogue_set_base(time_t base) {
  if (base == 0) {
    start_time = -1;
    start_time_round = 0;
  } else {
    struct tm *time = localtime(&base);
    start_time = (time->tm_hour * 120 + time->tm_min * 2) % 1440;
    start_time_round = start_time - (start_time % 24);
  }
  if (is_visible)
    layer_mark_dirty(analogue_layer);
}

/*
 * Mark progress on the analogue clock. Progress 1-54. Trigger an update of the layer
 */
EXTFN void analogue_set_progress(uint8_t progress_level_in) {
  progress_1 = start_time_round + ((int16_t) progress_level_in) * 20;
  if (progress_1 >= 1440) {
    progress_2 = progress_1 - 1440;
    progress_1 = 1439;
  } else {
    progress_2 = -1;
  }
  if (is_visible)
    layer_mark_dirty(analogue_layer);
}

/*
 * Plot the normal time display on the clock
 */
static void hands_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);

  time_t now = time(NULL);
  struct tm *t = localtime(&now);

  // minute/hour hand
  graphics_context_set_fill_color(ctx, MINUTE_HAND_COLOR);
  graphics_context_set_stroke_color(ctx, MINUTE_HAND_OUTLINE);

  gpath_rotate_to(minute_arrow, TRIG_MAX_ANGLE * t->tm_min / 60);
  gpath_draw_filled(ctx, minute_arrow);
  gpath_draw_outline(ctx, minute_arrow);

  #ifdef PBL_COLOR
    graphics_context_set_fill_color(ctx, HOUR_HAND_COLOR);
    graphics_context_set_stroke_color(ctx, HOUR_HAND_OUTLINE);
  #endif
  
  gpath_rotate_to(hour_arrow, (TRIG_MAX_ANGLE * (((t->tm_hour % 12) * 6) + (t->tm_min / 10))) / (12 * 6));
  gpath_draw_filled(ctx, hour_arrow);
  gpath_draw_outline(ctx, hour_arrow);

  // dot in the middle
  graphics_context_set_fill_color(ctx, CENTRE_OUTLINE);
  graphics_fill_rect(ctx, GRect(bounds.size.w / 2 - 2, bounds.size.h / 2 - 2, 5, 5), 1, GCornersAll);
  graphics_context_set_fill_color(ctx, CENTRE_COLOR);
  graphics_fill_rect(ctx, GRect(bounds.size.w / 2 - 1, bounds.size.h / 2 - 1, 3, 3), 0, GCornersAll);
}

/*
 * Trigger the refresh of the time
 */
EXTFN void analogue_minute_tick() {
  if (is_visible)
    layer_mark_dirty(hands_layer);
}

/**
 * Load the analogue clock watch face
 */
EXTFN void analogue_window_load(Window *window) {

  Layer *window_layer = window_get_root_layer(window);

  // Init internal state used by bg_update_proc
  show_smart_points = false;
  from_time = 0;
  to_time = 0;
  start_time = -1;
  start_time_round = 0;
  progress_1 = -1;
  progress_2 = -1;

  // init layers
  analogue_layer = macro_layer_create(ANALOGUE_START, window_layer, bg_update_proc);

  // init hands
  // init hand paths
  minute_arrow = gpath_create(&MINUTE_HAND_POINTS);
  hour_arrow = gpath_create(&HOUR_HAND_POINTS);

  GPoint center = grect_center_point(&GRect(0, 0, 144, 144));
  gpath_move_to(minute_arrow, center);
  gpath_move_to(hour_arrow, center);

  hands_layer = macro_layer_create(GRect(0, 0, 144, 144), analogue_layer, hands_update_proc);
}

/*
 * Triggered when the sliding in/out of the analogue face completes
 */
static void animation_stopped(Animation *animation, bool finished, void *data) {
  if (is_visible) {
    bed_visible(false);
  }
  if (g_call_post_init) {
    app_timer_register(250, post_init_hook, NULL);
  }
}

/*
 * Build and start an animation used when making the face visible or invisible
 */
static void start_animation(GRect *start, GRect *finish) {
  analogue_animation = property_animation_create_layer_frame(analogue_layer, start, finish);
  animation_set_duration((Animation*) analogue_animation, ANIMATE_ANALOGUE_DURATION);
  animation_set_handlers((Animation*) analogue_animation, (AnimationHandlers ) { .stopped = (AnimationStoppedHandler) animation_stopped, }, NULL /* callback data */);
  animation_schedule((Animation*) analogue_animation);
}

/*
 * Make the analogue watchface visible or invisible
 */
EXTFN void analogue_visible(bool visible, bool call_post_init) {
  if (visible && !is_visible) {
    start_animation(&ANALOGUE_START, &ANALOGUE_FINISH);
  } else if (!visible && is_visible) {
    start_animation(&ANALOGUE_FINISH, &ANALOGUE_START);
    bed_visible(true);
  } else if (call_post_init) {
    app_timer_register(250, post_init_hook, NULL);
  }
  g_call_post_init = call_post_init;
  is_visible = visible;
}

#ifndef PBL_PLATFORM_APLITE 
/*
 * Unload the analogue watchface
 */
EXTFN void analogue_window_unload() {
  gpath_destroy(minute_arrow);
  gpath_destroy(hour_arrow);
  layer_destroy(hands_layer);
  layer_destroy(analogue_layer);
}
#endif
  
#endif

