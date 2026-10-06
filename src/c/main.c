#include <pebble.h>
#include <stdio.h>
#include <string.h>

#define MAX_PROJECTS 25
#define MAX_NAME_LEN 16
#define PERSIST_KEY_COUNT 100
#define PERSIST_KEY_PROJECTS 101
#define PERSIST_KEY_LAST_PROJECT 102

// Color Definitions with Monochrome Fallback
#define COLOR_BG        PBL_IF_COLOR_ELSE(GColorArmyGreen, GColorWhite)
#define COLOR_ACCENT    PBL_IF_COLOR_ELSE(GColorOrange,    GColorBlack)
#define COLOR_TEXT_MAIN PBL_IF_COLOR_ELSE(GColorWhite,     GColorBlack)
#define COLOR_TEXT_SEL  PBL_IF_COLOR_ELSE(GColorBlack,     GColorWhite)
#define COLOR_MONKEY    PBL_IF_COLOR_ELSE(GColorPastelYellow, GColorBlack)

typedef struct {
  char name[MAX_NAME_LEN + 1];
  int last_word_count;
} Project;

typedef enum {
  APP_STATE_SPLASH,
  APP_STATE_PROJECT_HUB,
  APP_STATE_CREATE_NAME,
  APP_STATE_LOAD_LIST,
  APP_STATE_INPUT_COUNT,
  APP_STATE_CONFIRM_MENU,
  APP_STATE_INPUT_TIME,
  APP_STATE_WRITING,
  APP_STATE_FINISHED,
  APP_STATE_INPUT_END_COUNT,
  APP_STATE_SPRINT_SUMMARY,
  APP_STATE_EXITING
} AppState;

static Window *s_main_window;
static AppState s_app_state = APP_STATE_SPLASH;
static AppTimer *s_splash_timer = NULL;
static AppTimer *s_exit_timer = NULL;

static bool s_navigated_from_load = false;

// --- Persistent Project Store ---
static Project s_projects[MAX_PROJECTS];
static int s_num_projects = 0;
static int s_current_project_idx = -1;
static int s_last_sprinted_idx = -1;

// --- Screen 0: Splash ---
static Layer *s_splash_canvas_layer;

// --- Screen 1: Project Hub ---
static TextLayer *s_hub_title_layer;
static TextLayer *s_hub_opt_resume_layer;
static TextLayer *s_hub_opt1_layer;
static TextLayer *s_hub_opt2_layer;
static Layer *s_hub_highlight_layer;
static int s_hub_index = 0;

// --- Screen 2: Create Name ---
static const char s_char_set[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
#define CHAR_SET_LEN 37
static int s_name_char_indices[MAX_NAME_LEN];
static int s_name_cursor = 0;
static int s_name_last_char_pos = 0;
static TextLayer *s_name_title_layer;
static TextLayer *s_name_preview_layer;
static TextLayer *s_name_cur_char_layer;
static TextLayer *s_name_set_layer;
static TextLayer *s_name_amend_layer;
static Layer *s_name_highlight_layer;

// --- Screen 3: Load List ---
static TextLayer *s_load_title_layer;
static TextLayer *s_load_items_layers[3];
static TextLayer *s_load_empty_layer;
static TextLayer *s_load_back_layer;
static Layer *s_load_highlight_layer;
static int s_load_selected_idx = 0;

// --- Screen 4 & 6: Word Count Input (Start/End) ---
static TextLayer *s_count_title_layer;
static TextLayer *s_count_digit_layers[6];
static TextLayer *s_count_set_layer;
static TextLayer *s_count_amend_layer;
static Layer *s_count_highlight_layer;

static int s_count_digits[6] = {0, 0, 0, 0, 0, 0};
static char s_count_digit_buffers[6][2];
static int s_count_cursor = 0;
static int s_start_count = 0;
static int s_end_count = 0;

// --- Screen 5: Confirm Menu ---
static TextLayer *s_locked_title_layer;
static TextLayer *s_count_display_layer;
static TextLayer *s_menu_option1_layer;
static TextLayer *s_menu_option2_layer;
static Layer *s_menu_highlight_layer;
static int s_confirm_menu_index = 0;

// --- Screen 6: Sprint Duration ---
static TextLayer *s_time_title_layer;
static TextLayer *s_time_digit_layers[4];
static TextLayer *s_time_colon_layer;
static TextLayer *s_time_set_layer;
static TextLayer *s_time_amend_layer;
static Layer *s_time_highlight_layer;

static int s_time_digits[4] = {0, 0, 0, 0};
static char s_time_digit_buffers[4][2];
static int s_time_cursor = 0;
static int s_final_hours = 0;
static int s_final_minutes = 0;

// --- Screen 7, 8, & Exit: Writing, Finished, Animation ---
static TextLayer *s_writing_layer;
static Layer *s_monkey_anim_layer;
static AppTimer *s_sprint_timer = NULL;
static int s_seconds_remaining = 0;
static int s_anim_frame = 0; // 0 to 3 repeating

// --- Screen 9: Results / Post-Sprint Summary ---
static TextLayer *s_summary_stats_layer;
static TextLayer *s_summary_opt1_layer;
static TextLayer *s_summary_opt2_layer;
static Layer *s_summary_highlight_layer;
static int s_summary_menu_index = 0;

static const uint32_t s_vibe_segments[] = { 350, 200, 350, 200, 350 };
static const VibePattern s_triple_vibe = {
  .durations = (uint32_t *)s_vibe_segments,
  .num_segments = ARRAY_LENGTH(s_vibe_segments),
};

static void set_app_state(AppState new_state);

// --- Storage Handlers ---
static void load_projects(void) {
  if (persist_exists(PERSIST_KEY_COUNT)) {
    s_num_projects = persist_read_int(PERSIST_KEY_COUNT);
    if (s_num_projects > MAX_PROJECTS) s_num_projects = MAX_PROJECTS;
    if (s_num_projects > 0 && persist_exists(PERSIST_KEY_PROJECTS)) {
      persist_read_data(PERSIST_KEY_PROJECTS, s_projects, sizeof(Project) * s_num_projects);
    }
  } else {
    s_num_projects = 0;
  }

  if (persist_exists(PERSIST_KEY_LAST_PROJECT)) {
    s_last_sprinted_idx = persist_read_int(PERSIST_KEY_LAST_PROJECT);
    if (s_last_sprinted_idx >= s_num_projects) {
      s_last_sprinted_idx = -1;
    }
  } else {
    s_last_sprinted_idx = -1;
  }
}

static void save_projects(void) {
  persist_write_int(PERSIST_KEY_COUNT, s_num_projects);
  if (s_num_projects > 0) {
    persist_write_data(PERSIST_KEY_PROJECTS, s_projects, sizeof(Project) * s_num_projects);
  }
  if (s_last_sprinted_idx >= 0) {
    persist_write_int(PERSIST_KEY_LAST_PROJECT, s_last_sprinted_idx);
  }
}

// --- Bubble Text Renderer ---
static void draw_bubble_line(GContext *ctx, const char *text, GFont font, int y, int w) {
  graphics_context_set_text_color(ctx, COLOR_ACCENT);
  for (int dx = -2; dx <= 2; dx++) {
    for (int dy = -2; dy <= 2; dy++) {
      if (dx != 0 || dy != 0) {
        graphics_draw_text(ctx, text, font, GRect(dx, y + dy, w, 40),
                           GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
      }
    }
  }
  graphics_context_set_text_color(ctx, COLOR_TEXT_MAIN);
  graphics_draw_text(ctx, text, font, GRect(0, y, w, 40),
                     GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}

static void splash_canvas_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  GFont bubble_font = fonts_get_system_font(FONT_KEY_BITHAM_30_BLACK);

  int block_height = 104;
  int start_y = (bounds.size.h - block_height) / 2;

  draw_bubble_line(ctx, "Typing", bubble_font, start_y, bounds.size.w);
  draw_bubble_line(ctx, "Monkey", bubble_font, start_y + 35, bounds.size.w);
  draw_bubble_line(ctx, "SPRINT", bubble_font, start_y + 70, bounds.size.w);
}

// --- Side-On Monkey & Typewriter Animation ---
static void monkey_anim_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  int mid_x = bounds.size.w / 2;

  if (s_app_state == APP_STATE_WRITING) {
    int base_x = mid_x - 36;
    int base_y = 6;

    // 1. Desk Surface
    graphics_context_set_stroke_color(ctx, COLOR_TEXT_MAIN);
    graphics_draw_line(ctx, GPoint(base_x - 12, base_y + 44), GPoint(base_x + 82, base_y + 44));

    // 2. Typewriter (Body & Roller)
    graphics_context_set_fill_color(ctx, COLOR_ACCENT);
    graphics_fill_rect(ctx, GRect(base_x + 40, base_y + 30, 26, 14), 2, GCornersAll); // body
    graphics_context_set_fill_color(ctx, COLOR_TEXT_MAIN);
    graphics_fill_rect(ctx, GRect(base_x + 45, base_y + 24, 16, 6), 1, GCornersAll);  // roller

    // 3. Paper Sheet (Jitters up/down between frames)
    int paper_offset = (s_anim_frame % 2 == 0) ? 0 : 3;
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_rect(ctx, GRect(base_x + 48, base_y + 12 + paper_offset, 10, 14), 0, GCornerNone);

    // 4. Monkey Body (Side View)
    graphics_context_set_fill_color(ctx, COLOR_MONKEY);
    graphics_fill_rect(ctx, GRect(base_x + 10, base_y + 20, 18, 22), 4, GCornersAll); // torso
    graphics_fill_circle(ctx, GPoint(base_x + 19, base_y + 13), 9);                     // head
    graphics_fill_circle(ctx, GPoint(base_x + 8, base_y + 12), 4);                      // left ear
    graphics_context_set_fill_color(ctx, COLOR_ACCENT);
    graphics_fill_circle(ctx, GPoint(base_x + 24, base_y + 15), 5);                     // muzzle

    // Eye
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_circle(ctx, GPoint(base_x + 22, base_y + 12), 2);

    // 5. Monkey Tail (Wags across frames)
    graphics_context_set_stroke_color(ctx, COLOR_MONKEY);
    graphics_context_set_stroke_width(ctx, 2);
    if (s_anim_frame == 0 || s_anim_frame == 2) {
      graphics_draw_line(ctx, GPoint(base_x + 10, base_y + 36), GPoint(base_x - 2, base_y + 30));
      graphics_draw_line(ctx, GPoint(base_x - 2, base_y + 30), GPoint(base_x - 4, base_y + 20));
    } else {
      graphics_draw_line(ctx, GPoint(base_x + 10, base_y + 36), GPoint(base_x - 4, base_y + 36));
      graphics_draw_line(ctx, GPoint(base_x - 4, base_y + 36), GPoint(base_x - 8, base_y + 28));
    }

    // 6. Typing Hand / Arm (Strikes the keys)
    int hand_y = (s_anim_frame == 1 || s_anim_frame == 3) ? (base_y + 31) : (base_y + 23);
    graphics_draw_line(ctx, GPoint(base_x + 20, base_y + 24), GPoint(base_x + 35, hand_y));
    graphics_context_set_fill_color(ctx, COLOR_ACCENT);
    graphics_fill_circle(ctx, GPoint(base_x + 36, hand_y), 3); // hand paws keys

  } else if (s_app_state == APP_STATE_FINISHED) {
    // Monkey Thumbs Up Celebration
    int cx = mid_x;
    int cy = 26;

    // Body & Head
    graphics_context_set_fill_color(ctx, COLOR_MONKEY);
    graphics_fill_circle(ctx, GPoint(cx, cy), 12);               // Head
    graphics_fill_circle(ctx, GPoint(cx - 13, cy - 2), 5);        // Left ear
    graphics_fill_circle(ctx, GPoint(cx + 13, cy - 2), 5);        // Right ear
    graphics_context_set_fill_color(ctx, COLOR_ACCENT);
    graphics_fill_circle(ctx, GPoint(cx, cy + 4), 6);            // Snout

    // Smile Eyes
    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_context_set_stroke_width(ctx, 2);
    graphics_draw_line(ctx, GPoint(cx - 5, cy - 1), GPoint(cx - 2, cy - 1));
    graphics_draw_line(ctx, GPoint(cx + 2, cy - 1), GPoint(cx + 5, cy - 1));

    // Thumbs-Up Hand
    graphics_context_set_fill_color(ctx, COLOR_ACCENT);
    graphics_fill_rect(ctx, GRect(cx + 16, cy - 2, 7, 10), 2, GCornersAll); // fist
    graphics_fill_rect(ctx, GRect(cx + 18, cy - 8, 4, 8), 1, GCornersAll);  // thumb pointing up
  }
}

static void box_draw_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, COLOR_ACCENT);
  graphics_fill_rect(ctx, bounds, 3, GCornersAll);
}

static TextLayer* create_themed_text_layer(GRect frame, const char *font_key, GTextAlignment align) {
  TextLayer *layer = text_layer_create(frame);
  text_layer_set_background_color(layer, GColorClear);
  text_layer_set_text_color(layer, COLOR_TEXT_MAIN);
  text_layer_set_font(layer, fonts_get_system_font(font_key));
  text_layer_set_text_alignment(layer, align);
  return layer;
}

static int calculate_count_digits(void) {
  int total = 0;
  for (int i = 0; i < 6; i++) {
    total = total * 10 + s_count_digits[i];
  }
  return total;
}

static void load_digits_from_number(int value) {
  for (int i = 5; i >= 0; i--) {
    s_count_digits[i] = value % 10;
    value /= 10;
  }
}

static void refresh_count_display(int index) {
  snprintf(s_count_digit_buffers[index], sizeof(s_count_digit_buffers[index]), "%d", s_count_digits[index]);
  text_layer_set_text(s_count_digit_layers[index], s_count_digit_buffers[index]);
}

static void refresh_all_count_displays(void) {
  for (int i = 0; i < 6; i++) refresh_count_display(i);
}

static void refresh_time_display(int index) {
  snprintf(s_time_digit_buffers[index], sizeof(s_time_digit_buffers[index]), "%d", s_time_digits[index]);
  text_layer_set_text(s_time_digit_layers[index], s_time_digit_buffers[index]);
}

static void refresh_name_preview(void) {
  static char name_buf[MAX_NAME_LEN + 1];
  for (int i = 0; i < MAX_NAME_LEN; i++) {
    name_buf[i] = s_char_set[s_name_char_indices[i]];
  }
  name_buf[MAX_NAME_LEN] = '\0';
  text_layer_set_text(s_name_preview_layer, name_buf);

  if (s_name_cursor < MAX_NAME_LEN) {
    static char cur_buf[32];
    snprintf(cur_buf, sizeof(cur_buf), "Pos %d: [%c]", s_name_cursor + 1, s_char_set[s_name_char_indices[s_name_cursor]]);
    text_layer_set_text(s_name_cur_char_layer, cur_buf);
  } else {
    text_layer_set_text(s_name_cur_char_layer, "Ready to finalize?");
  }
}

static void update_name_highlight(void) {
  GRect win_bounds = layer_get_bounds(window_get_root_layer(s_main_window));
  int mid_x = win_bounds.size.w / 2;

  text_layer_set_text_color(s_name_cur_char_layer, (s_name_cursor < MAX_NAME_LEN) ? COLOR_TEXT_SEL : COLOR_TEXT_MAIN);
  text_layer_set_text_color(s_name_set_layer,      (s_name_cursor == 16)           ? COLOR_TEXT_SEL : COLOR_TEXT_MAIN);
  text_layer_set_text_color(s_name_amend_layer,    (s_name_cursor == 17)           ? COLOR_TEXT_SEL : COLOR_TEXT_MAIN);

  if (s_name_cursor < MAX_NAME_LEN) {
    layer_set_frame(s_name_highlight_layer, GRect(mid_x - 60, 70, 120, 24));
  } else if (s_name_cursor == 16) {
    layer_set_frame(s_name_highlight_layer, GRect(mid_x - 30, 100, 60, 24));
  } else {
    layer_set_frame(s_name_highlight_layer, GRect(mid_x - 38, 130, 76, 24));
  }
  layer_mark_dirty(s_name_highlight_layer);
}

static bool has_last_project(void) {
  return (s_last_sprinted_idx >= 0 && s_last_sprinted_idx < s_num_projects);
}

static void update_hub_highlight(void) {
  GRect win_bounds = layer_get_bounds(window_get_root_layer(s_main_window));
  int mid_x = win_bounds.size.w / 2;

  if (has_last_project()) {
    text_layer_set_text_color(s_hub_opt_resume_layer, (s_hub_index == 0) ? COLOR_TEXT_SEL : COLOR_TEXT_MAIN);
    text_layer_set_text_color(s_hub_opt1_layer,        (s_hub_index == 1) ? COLOR_TEXT_SEL : COLOR_TEXT_MAIN);
    text_layer_set_text_color(s_hub_opt2_layer,        (s_hub_index == 2) ? COLOR_TEXT_SEL : COLOR_TEXT_MAIN);

    if (s_hub_index == 0) {
      layer_set_frame(s_hub_highlight_layer, GRect(mid_x - 66, 42, 132, 34));
    } else if (s_hub_index == 1) {
      layer_set_frame(s_hub_highlight_layer, GRect(mid_x - 62, 84, 124, 26));
    } else {
      layer_set_frame(s_hub_highlight_layer, GRect(mid_x - 62, 118, 124, 26));
    }
  } else {
    text_layer_set_text_color(s_hub_opt1_layer, (s_hub_index == 0) ? COLOR_TEXT_SEL : COLOR_TEXT_MAIN);
    text_layer_set_text_color(s_hub_opt2_layer, (s_hub_index == 1) ? COLOR_TEXT_SEL : COLOR_TEXT_MAIN);

    if (s_hub_index == 0) {
      layer_set_frame(s_hub_highlight_layer, GRect(mid_x - 60, 62, 120, 30));
    } else {
      layer_set_frame(s_hub_highlight_layer, GRect(mid_x - 60, 104, 120, 30));
    }
  }
  layer_mark_dirty(s_hub_highlight_layer);
}

static void refresh_load_list(void) {
  GRect win_bounds = layer_get_bounds(window_get_root_layer(s_main_window));
  int mid_x = win_bounds.size.w / 2;

  if (s_num_projects == 0) {
    layer_set_hidden(text_layer_get_layer(s_load_empty_layer), false);
    layer_set_hidden(text_layer_get_layer(s_load_back_layer), false);
    text_layer_set_text_color(s_load_back_layer, COLOR_TEXT_SEL);
    layer_set_frame(s_load_highlight_layer, GRect(mid_x - 32, 110, 64, 28));
    for (int i = 0; i < 3; i++) {
      layer_set_hidden(text_layer_get_layer(s_load_items_layers[i]), true);
    }
  } else {
    layer_set_hidden(text_layer_get_layer(s_load_empty_layer), true);
    layer_set_hidden(text_layer_get_layer(s_load_back_layer), true);

    int start = (s_load_selected_idx / 3) * 3;
    for (int i = 0; i < 3; i++) {
      int p_idx = start + i;
      if (p_idx < s_num_projects) {
        static char item_buf[3][32];
        snprintf(item_buf[i], sizeof(item_buf[i]), "%s", s_projects[p_idx].name);
        text_layer_set_text(s_load_items_layers[i], item_buf[i]);
        layer_set_hidden(text_layer_get_layer(s_load_items_layers[i]), false);
        
        bool is_selected = (p_idx == s_load_selected_idx);
        text_layer_set_text_color(s_load_items_layers[i], is_selected ? COLOR_TEXT_SEL : COLOR_TEXT_MAIN);
      } else {
        layer_set_hidden(text_layer_get_layer(s_load_items_layers[i]), true);
      }
    }
    int offset = s_load_selected_idx % 3;
    layer_set_frame(s_load_highlight_layer, GRect(mid_x - 66, 48 + (offset * 32), 132, 28));
  }
  layer_mark_dirty(s_load_highlight_layer);
}

static void update_count_highlight(void) {
  GRect win_bounds = layer_get_bounds(window_get_root_layer(s_main_window));
  int count_start_x = (win_bounds.size.w - 96) / 2;
  int mid_x = win_bounds.size.w / 2;

  for (int i = 0; i < 6; i++) {
    text_layer_set_text_color(s_count_digit_layers[i], (s_count_cursor == i) ? COLOR_TEXT_SEL : COLOR_TEXT_MAIN);
  }
  text_layer_set_text_color(s_count_set_layer,   (s_count_cursor == 6) ? COLOR_TEXT_SEL : COLOR_TEXT_MAIN);
  text_layer_set_text_color(s_count_amend_layer, (s_count_cursor == 7) ? COLOR_TEXT_SEL : COLOR_TEXT_MAIN);

  if (s_count_cursor < 6) {
    layer_set_frame(s_count_highlight_layer, GRect(count_start_x + (s_count_cursor * 16), 52, 14, 28));
  } else if (s_count_cursor == 6) {
    layer_set_frame(s_count_highlight_layer, GRect(mid_x - 30, 92, 60, 24));
  } else if (s_count_cursor == 7) {
    layer_set_frame(s_count_highlight_layer, GRect(mid_x - 38, 122, 76, 24));
  }
  layer_mark_dirty(s_count_highlight_layer);
}

static void update_time_highlight(void) {
  GRect win_bounds = layer_get_bounds(window_get_root_layer(s_main_window));
  int mid_x = win_bounds.size.w / 2;

  for (int i = 0; i < 4; i++) {
    text_layer_set_text_color(s_time_digit_layers[i], (s_time_cursor == i) ? COLOR_TEXT_SEL : COLOR_TEXT_MAIN);
  }
  text_layer_set_text_color(s_time_set_layer,   (s_time_cursor == 4) ? COLOR_TEXT_SEL : COLOR_TEXT_MAIN);
  text_layer_set_text_color(s_time_amend_layer, (s_time_cursor == 5) ? COLOR_TEXT_SEL : COLOR_TEXT_MAIN);

  if (s_time_cursor == 0) {
    layer_set_frame(s_time_highlight_layer, GRect(mid_x - 40, 52, 16, 28));
  } else if (s_time_cursor == 1) {
    layer_set_frame(s_time_highlight_layer, GRect(mid_x - 22, 52, 16, 28));
  } else if (s_time_cursor == 2) {
    layer_set_frame(s_time_highlight_layer, GRect(mid_x + 6, 52, 16, 28));
  } else if (s_time_cursor == 3) {
    layer_set_frame(s_time_highlight_layer, GRect(mid_x + 24, 52, 16, 28));
  } else if (s_time_cursor == 4) {
    layer_set_frame(s_time_highlight_layer, GRect(mid_x - 30, 92, 60, 24));
  } else if (s_time_cursor == 5) {
    layer_set_frame(s_time_highlight_layer, GRect(mid_x - 38, 122, 76, 24));
  }
  layer_mark_dirty(s_time_highlight_layer);
}

static void update_menu_highlight(void) {
  GRect win_bounds = layer_get_bounds(window_get_root_layer(s_main_window));
  int mid_x = win_bounds.size.w / 2;

  text_layer_set_text_color(s_menu_option1_layer, (s_confirm_menu_index == 0) ? COLOR_TEXT_SEL : COLOR_TEXT_MAIN);
  text_layer_set_text_color(s_menu_option2_layer, (s_confirm_menu_index == 1) ? COLOR_TEXT_SEL : COLOR_TEXT_MAIN);

  if (s_confirm_menu_index == 0) {
    layer_set_frame(s_menu_highlight_layer, GRect(mid_x - 64, 88, 128, 26));
  } else {
    layer_set_frame(s_menu_highlight_layer, GRect(mid_x - 68, 118, 136, 40));
  }
  layer_mark_dirty(s_menu_highlight_layer);
}

static void update_summary_highlight(void) {
  GRect win_bounds = layer_get_bounds(window_get_root_layer(s_main_window));
  int mid_x = win_bounds.size.w / 2;

  text_layer_set_text_color(s_summary_opt1_layer, (s_summary_menu_index == 0) ? COLOR_TEXT_SEL : COLOR_TEXT_MAIN);
  text_layer_set_text_color(s_summary_opt2_layer, (s_summary_menu_index == 1) ? COLOR_TEXT_SEL : COLOR_TEXT_MAIN);

  if (s_summary_menu_index == 0) {
    layer_set_frame(s_summary_highlight_layer, GRect(mid_x - 68, 82, 136, 38));
  } else {
    layer_set_frame(s_summary_highlight_layer, GRect(mid_x - 68, 124, 136, 34));
  }
  layer_mark_dirty(s_summary_highlight_layer);
}

static void update_timer_display(void) {
  static char buffer[128];
  int hrs = s_seconds_remaining / 3600;
  int mins = (s_seconds_remaining % 3600) / 60;
  int secs = s_seconds_remaining % 60;

  const char *pname = (s_current_project_idx >= 0) ? s_projects[s_current_project_idx].name : "SPRINT";

  if (s_seconds_remaining >= 60) {
    int display_mins = mins + (secs > 0 ? 1 : 0);
    int display_hrs = hrs;
    if (display_mins == 60) { display_hrs++; display_mins = 0; }
    snprintf(buffer, sizeof(buffer), "%s\n%02d:%02d\nStart: %d",
             pname, display_hrs, display_mins, s_start_count);
  } else {
    snprintf(buffer, sizeof(buffer), "%s\n:%02d left\nStart: %d",
             pname, secs, s_start_count);
  }
  text_layer_set_text(s_writing_layer, buffer);
  layer_mark_dirty(s_monkey_anim_layer);
}

static void timer_callback(void *data) {
  if (s_seconds_remaining > 0) {
    s_seconds_remaining--;
    s_anim_frame = (s_anim_frame + 1) % 4; // Advance monkey animation frame
    update_timer_display();
    s_sprint_timer = app_timer_register(1000, timer_callback, NULL);
  } else {
    s_sprint_timer = NULL;
    vibes_enqueue_custom_pattern(s_triple_vibe);
    set_app_state(APP_STATE_FINISHED);
  }
}

static void splash_timer_callback(void *data) {
  s_splash_timer = NULL;
  set_app_state(APP_STATE_PROJECT_HUB);
}

static void exit_timer_callback(void *data) {
  s_exit_timer = NULL;
  window_stack_pop(true);
}

// --- App State Switcher ---
static void set_app_state(AppState new_state) {
  if (s_app_state == APP_STATE_WRITING && new_state != APP_STATE_WRITING) {
    if (s_sprint_timer) {
      app_timer_cancel(s_sprint_timer);
      s_sprint_timer = NULL;
    }
  }

  s_app_state = new_state;

  // Splash
  bool show_splash = (s_app_state == APP_STATE_SPLASH);
  layer_set_hidden(s_splash_canvas_layer, !show_splash);

  // Hub
  bool show_hub = (s_app_state == APP_STATE_PROJECT_HUB);
  layer_set_hidden(text_layer_get_layer(s_hub_title_layer), !show_hub);
  layer_set_hidden(text_layer_get_layer(s_hub_opt_resume_layer), !show_hub || !has_last_project());
  layer_set_hidden(text_layer_get_layer(s_hub_opt1_layer), !show_hub);
  layer_set_hidden(text_layer_get_layer(s_hub_opt2_layer), !show_hub);
  layer_set_hidden(s_hub_highlight_layer, !show_hub);

  // Create Name
  bool show_create = (s_app_state == APP_STATE_CREATE_NAME);
  layer_set_hidden(text_layer_get_layer(s_name_title_layer), !show_create);
  layer_set_hidden(text_layer_get_layer(s_name_preview_layer), !show_create);
  layer_set_hidden(text_layer_get_layer(s_name_cur_char_layer), !show_create);
  layer_set_hidden(text_layer_get_layer(s_name_set_layer), !show_create);
  layer_set_hidden(text_layer_get_layer(s_name_amend_layer), !show_create);
  layer_set_hidden(s_name_highlight_layer, !show_create);

  // Load Screen
  bool show_load = (s_app_state == APP_STATE_LOAD_LIST);
  layer_set_hidden(text_layer_get_layer(s_load_title_layer), !show_load);
  layer_set_hidden(s_load_highlight_layer, !show_load);
  if (!show_load) {
    layer_set_hidden(text_layer_get_layer(s_load_empty_layer), true);
    layer_set_hidden(text_layer_get_layer(s_load_back_layer), true);
    for (int i = 0; i < 3; i++) {
      layer_set_hidden(text_layer_get_layer(s_load_items_layers[i]), true);
    }
  }

  // Word Count
  bool show_count = (s_app_state == APP_STATE_INPUT_COUNT || s_app_state == APP_STATE_INPUT_END_COUNT);
  layer_set_hidden(text_layer_get_layer(s_count_title_layer), !show_count);
  for (int i = 0; i < 6; i++) {
    layer_set_hidden(text_layer_get_layer(s_count_digit_layers[i]), !show_count);
  }
  layer_set_hidden(text_layer_get_layer(s_count_set_layer), !show_count);
  layer_set_hidden(text_layer_get_layer(s_count_amend_layer), !show_count);
  layer_set_hidden(s_count_highlight_layer, !show_count);

  // Confirm Menu
  bool show_menu = (s_app_state == APP_STATE_CONFIRM_MENU);
  layer_set_hidden(text_layer_get_layer(s_locked_title_layer), !show_menu);
  layer_set_hidden(text_layer_get_layer(s_count_display_layer), !show_menu);
  layer_set_hidden(text_layer_get_layer(s_menu_option1_layer), !show_menu);
  layer_set_hidden(text_layer_get_layer(s_menu_option2_layer), !show_menu);
  layer_set_hidden(s_menu_highlight_layer, !show_menu);

  // Sprint Time
  bool show_time = (s_app_state == APP_STATE_INPUT_TIME);
  layer_set_hidden(text_layer_get_layer(s_time_title_layer), !show_time);
  for (int i = 0; i < 4; i++) {
    layer_set_hidden(text_layer_get_layer(s_time_digit_layers[i]), !show_time);
  }
  layer_set_hidden(text_layer_get_layer(s_time_colon_layer), !show_time);
  layer_set_hidden(text_layer_get_layer(s_time_set_layer), !show_time);
  layer_set_hidden(text_layer_get_layer(s_time_amend_layer), !show_time);
  layer_set_hidden(s_time_highlight_layer, !show_time);

  // Writing / Finished / Exit
  bool show_writing = (s_app_state == APP_STATE_WRITING ||
                       s_app_state == APP_STATE_FINISHED ||
                       s_app_state == APP_STATE_EXITING);
  layer_set_hidden(text_layer_get_layer(s_writing_layer), !show_writing);
  layer_set_hidden(s_monkey_anim_layer, !(s_app_state == APP_STATE_WRITING || s_app_state == APP_STATE_FINISHED));

  // Results Summary
  bool show_summary = (s_app_state == APP_STATE_SPRINT_SUMMARY);
  layer_set_hidden(text_layer_get_layer(s_summary_stats_layer), !show_summary);
  layer_set_hidden(text_layer_get_layer(s_summary_opt1_layer), !show_summary);
  layer_set_hidden(text_layer_get_layer(s_summary_opt2_layer), !show_summary);
  layer_set_hidden(s_summary_highlight_layer, !show_summary);

  // Specific state setup
  if (s_app_state == APP_STATE_SPLASH) {
    layer_mark_dirty(s_splash_canvas_layer);
    s_splash_timer = app_timer_register(3000, splash_timer_callback, NULL);
  } else if (s_app_state == APP_STATE_PROJECT_HUB) {
    s_hub_index = 0;
    GRect win_bounds = layer_get_bounds(window_get_root_layer(s_main_window));

    if (has_last_project()) {
      static char last_proj_label[48];
      snprintf(last_proj_label, sizeof(last_proj_label), "Load\n%s", s_projects[s_last_sprinted_idx].name);
      text_layer_set_text(s_hub_opt_resume_layer, last_proj_label);

      layer_set_frame(text_layer_get_layer(s_hub_title_layer), GRect(0, 10, win_bounds.size.w, 24));
      layer_set_frame(text_layer_get_layer(s_hub_opt_resume_layer), GRect(0, 42, win_bounds.size.w, 34));
      layer_set_frame(text_layer_get_layer(s_hub_opt1_layer), GRect(0, 86, win_bounds.size.w, 24));
      layer_set_frame(text_layer_get_layer(s_hub_opt2_layer), GRect(0, 120, win_bounds.size.w, 24));
    } else {
      layer_set_frame(text_layer_get_layer(s_hub_title_layer), GRect(0, 15, win_bounds.size.w, 26));
      layer_set_frame(text_layer_get_layer(s_hub_opt1_layer), GRect(0, 66, win_bounds.size.w, 24));
      layer_set_frame(text_layer_get_layer(s_hub_opt2_layer), GRect(0, 108, win_bounds.size.w, 24));
    }
    update_hub_highlight();
  } else if (s_app_state == APP_STATE_CREATE_NAME) {
    s_name_char_indices[0] = 1;
    for (int i = 1; i < MAX_NAME_LEN; i++) s_name_char_indices[i] = 0;
    s_name_cursor = 0;
    s_name_last_char_pos = 0;
    refresh_name_preview();
    update_name_highlight();
  } else if (s_app_state == APP_STATE_LOAD_LIST) {
    s_load_selected_idx = 0;
    refresh_load_list();
  } else if (s_app_state == APP_STATE_INPUT_COUNT) {
    text_layer_set_text(s_count_title_layer, "START WORD COUNT");
    s_count_cursor = 0;
    update_count_highlight();
  } else if (s_app_state == APP_STATE_CONFIRM_MENU) {
    static char count_str[48];
    snprintf(count_str, sizeof(count_str), "Count: %d", s_start_count);
    text_layer_set_text(s_count_display_layer, count_str);
    s_confirm_menu_index = 0;
    update_menu_highlight();
  } else if (s_app_state == APP_STATE_INPUT_TIME) {
    s_time_cursor = 0;
    update_time_highlight();
  } else if (s_app_state == APP_STATE_WRITING) {
    s_seconds_remaining = (s_final_hours * 3600) + (s_final_minutes * 60);
    if (s_seconds_remaining == 0) s_seconds_remaining = 60;
    s_anim_frame = 0;
    update_timer_display();
    s_sprint_timer = app_timer_register(1000, timer_callback, NULL);
  } else if (s_app_state == APP_STATE_FINISHED) {
    static char finish_msg[96];
    snprintf(finish_msg, sizeof(finish_msg), "TIME'S UP!\n\nMiddle: Log End");
    text_layer_set_text(s_writing_layer, finish_msg);
    layer_mark_dirty(s_monkey_anim_layer);
  } else if (s_app_state == APP_STATE_INPUT_END_COUNT) {
    text_layer_set_text(s_count_title_layer, "END WORD COUNT");
    load_digits_from_number(s_start_count);
    refresh_all_count_displays();
    s_count_cursor = 0;
    update_count_highlight();
  } else if (s_app_state == APP_STATE_SPRINT_SUMMARY) {
    int words_written = s_end_count - s_start_count;
    if (s_current_project_idx >= 0) {
      s_projects[s_current_project_idx].last_word_count = s_end_count;
      s_last_sprinted_idx = s_current_project_idx;
      save_projects();
    }

    static char stats_msg[64];
    snprintf(stats_msg, sizeof(stats_msg), "+%d WORDS\n(%d -> %d)", words_written, s_start_count, s_end_count);
    text_layer_set_text(s_summary_stats_layer, stats_msg);

    static char opt1_msg[64];
    const char *pname = (s_current_project_idx >= 0) ? s_projects[s_current_project_idx].name : "Project";
    snprintf(opt1_msg, sizeof(opt1_msg), "New Sprint for\n%s", pname);
    text_layer_set_text(s_summary_opt1_layer, opt1_msg);

    s_summary_menu_index = 0;
    update_summary_highlight();
  } else if (s_app_state == APP_STATE_EXITING) {
    text_layer_set_text(s_writing_layer, "Great work,\nbye for now!");
    s_exit_timer = app_timer_register(1800, exit_timer_callback, NULL);
  }
}

// --- BUTTONS ---

// UP BUTTON (Top)
static void up_click_handler(ClickRecognizerRef recognizer, void *context) {
  if (s_app_state == APP_STATE_PROJECT_HUB) {
    int max_opt = has_last_project() ? 3 : 2;
    s_hub_index = (s_hub_index - 1 + max_opt) % max_opt;
    update_hub_highlight();
  } else if (s_app_state == APP_STATE_CREATE_NAME) {
    if (s_name_cursor < MAX_NAME_LEN) {
      s_name_char_indices[s_name_cursor] = (s_name_char_indices[s_name_cursor] + 1) % CHAR_SET_LEN;
      refresh_name_preview();
    } else if (s_name_cursor == 17) {
      s_name_cursor = 16;
      update_name_highlight();
    }
  } else if (s_app_state == APP_STATE_LOAD_LIST) {
    if (s_num_projects > 0) {
      s_load_selected_idx = (s_load_selected_idx - 1 + s_num_projects) % s_num_projects;
      refresh_load_list();
    }
  } else if (s_app_state == APP_STATE_INPUT_COUNT || s_app_state == APP_STATE_INPUT_END_COUNT) {
    if (s_count_cursor < 6) {
      s_count_digits[s_count_cursor] = (s_count_digits[s_count_cursor] + 1) % 10;
      refresh_count_display(s_count_cursor);
    } else if (s_count_cursor == 7) {
      s_count_cursor = 6;
      update_count_highlight();
    }
  } else if (s_app_state == APP_STATE_CONFIRM_MENU) {
    s_confirm_menu_index = (s_confirm_menu_index == 0) ? 1 : 0;
    update_menu_highlight();
  } else if (s_app_state == APP_STATE_INPUT_TIME) {
    if (s_time_cursor < 4) {
      int max_val = (s_time_cursor == 2) ? 5 : 9;
      s_time_digits[s_time_cursor] = (s_time_digits[s_time_cursor] + 1) % (max_val + 1);
      refresh_time_display(s_time_cursor);
    } else if (s_time_cursor == 5) {
      s_time_cursor = 4;
      update_time_highlight();
    }
  } else if (s_app_state == APP_STATE_SPRINT_SUMMARY) {
    s_summary_menu_index = (s_summary_menu_index == 0) ? 1 : 0;
    update_summary_highlight();
  }
}

// DOWN BUTTON (Bottom)
static void down_click_handler(ClickRecognizerRef recognizer, void *context) {
  if (s_app_state == APP_STATE_PROJECT_HUB) {
    int max_opt = has_last_project() ? 3 : 2;
    s_hub_index = (s_hub_index + 1) % max_opt;
    update_hub_highlight();
  } else if (s_app_state == APP_STATE_CREATE_NAME) {
    if (s_name_cursor < MAX_NAME_LEN) {
      s_name_char_indices[s_name_cursor] = (s_name_char_indices[s_name_cursor] - 1 + CHAR_SET_LEN) % CHAR_SET_LEN;
      refresh_name_preview();
    } else if (s_name_cursor == 16) {
      s_name_cursor = 17;
      update_name_highlight();
    }
  } else if (s_app_state == APP_STATE_LOAD_LIST) {
    if (s_num_projects > 0) {
      s_load_selected_idx = (s_load_selected_idx + 1) % s_num_projects;
      refresh_load_list();
    }
  } else if (s_app_state == APP_STATE_INPUT_COUNT || s_app_state == APP_STATE_INPUT_END_COUNT) {
    if (s_count_cursor < 6) {
      s_count_digits[s_count_cursor] = (s_count_digits[s_count_cursor] - 1 + 10) % 10;
      refresh_count_display(s_count_cursor);
    } else if (s_count_cursor == 6) {
      s_count_cursor = 7;
      update_count_highlight();
    }
  } else if (s_app_state == APP_STATE_CONFIRM_MENU) {
    s_confirm_menu_index = (s_confirm_menu_index == 1) ? 0 : 1;
    update_menu_highlight();
  } else if (s_app_state == APP_STATE_INPUT_TIME) {
    if (s_time_cursor < 4) {
      int max_val = (s_time_cursor == 2) ? 5 : 9;
      s_time_digits[s_time_cursor] = (s_time_digits[s_time_cursor] - 1 + (max_val + 1)) % (max_val + 1);
      refresh_time_display(s_time_cursor);
    } else if (s_time_cursor == 4) {
      s_time_cursor = 5;
      update_time_highlight();
    }
  } else if (s_app_state == APP_STATE_SPRINT_SUMMARY) {
    s_summary_menu_index = (s_summary_menu_index == 1) ? 0 : 1;
    update_summary_highlight();
  }
}

// SELECT BUTTON (Middle)
static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
  if (s_app_state == APP_STATE_SPLASH) {
    if (s_splash_timer) {
      app_timer_cancel(s_splash_timer);
      s_splash_timer = NULL;
    }
    set_app_state(APP_STATE_PROJECT_HUB);
  } else if (s_app_state == APP_STATE_PROJECT_HUB) {
    if (has_last_project()) {
      if (s_hub_index == 0) {
        s_current_project_idx = s_last_sprinted_idx;
        s_start_count = s_projects[s_current_project_idx].last_word_count;
        load_digits_from_number(s_start_count);
        refresh_all_count_displays();
        s_navigated_from_load = true;
        set_app_state(APP_STATE_INPUT_TIME);
      } else if (s_hub_index == 1) {
        set_app_state(APP_STATE_CREATE_NAME);
      } else {
        set_app_state(APP_STATE_LOAD_LIST);
      }
    } else {
      if (s_hub_index == 0) {
        set_app_state(APP_STATE_CREATE_NAME);
      } else {
        set_app_state(APP_STATE_LOAD_LIST);
      }
    }
  } else if (s_app_state == APP_STATE_CREATE_NAME) {
    if (s_name_cursor < MAX_NAME_LEN) {
      s_name_last_char_pos = s_name_cursor;

      if (s_name_cursor >= 2 &&
          s_name_char_indices[s_name_cursor] == 0 &&
          s_name_char_indices[s_name_cursor - 1] == 0 &&
          s_name_char_indices[s_name_cursor - 2] == 0) {
        s_name_cursor = 16;
      } else {
        s_name_cursor++;
      }
      update_name_highlight();
      refresh_name_preview();
    } else if (s_name_cursor == 16) {
      char clean_name[MAX_NAME_LEN + 1];
      for (int i = 0; i < MAX_NAME_LEN; i++) {
        clean_name[i] = s_char_set[s_name_char_indices[i]];
      }
      clean_name[MAX_NAME_LEN] = '\0';

      for (int i = MAX_NAME_LEN - 1; i >= 0; i--) {
        if (clean_name[i] == ' ') clean_name[i] = '\0';
        else break;
      }
      if (strlen(clean_name) == 0) strcpy(clean_name, "UNTITLED");

      if (s_num_projects < MAX_PROJECTS) {
        s_current_project_idx = s_num_projects;
        strncpy(s_projects[s_current_project_idx].name, clean_name, MAX_NAME_LEN);
        s_projects[s_current_project_idx].last_word_count = 0;
        s_num_projects++;
      } else {
        s_current_project_idx = MAX_PROJECTS - 1;
        strncpy(s_projects[s_current_project_idx].name, clean_name, MAX_NAME_LEN);
        s_projects[s_current_project_idx].last_word_count = 0;
      }
      save_projects();

      s_navigated_from_load = false;
      s_start_count = 0;
      load_digits_from_number(0);
      refresh_all_count_displays();
      set_app_state(APP_STATE_INPUT_COUNT);
    } else if (s_name_cursor == 17) {
      s_name_cursor = 0;
      update_name_highlight();
      refresh_name_preview();
    }
  } else if (s_app_state == APP_STATE_LOAD_LIST) {
    if (s_num_projects == 0) {
      set_app_state(APP_STATE_PROJECT_HUB);
    } else {
      s_current_project_idx = s_load_selected_idx;
      s_start_count = s_projects[s_current_project_idx].last_word_count;
      load_digits_from_number(s_start_count);
      refresh_all_count_displays();
      s_navigated_from_load = true;
      set_app_state(APP_STATE_CONFIRM_MENU);
    }
  } else if (s_app_state == APP_STATE_INPUT_COUNT) {
    if (s_count_cursor < 6) {
      s_count_cursor++;
      update_count_highlight();
    } else if (s_count_cursor == 6) {
      s_start_count = calculate_count_digits();
      set_app_state(APP_STATE_CONFIRM_MENU);
    } else if (s_count_cursor == 7) {
      s_count_cursor = 0;
      update_count_highlight();
    }
  } else if (s_app_state == APP_STATE_CONFIRM_MENU) {
    if (s_confirm_menu_index == 0) {
      set_app_state(APP_STATE_INPUT_TIME);
    } else {
      set_app_state(APP_STATE_INPUT_COUNT);
    }
  } else if (s_app_state == APP_STATE_INPUT_TIME) {
    if (s_time_cursor < 4) {
      s_time_cursor++;
      update_time_highlight();
    } else if (s_time_cursor == 4) {
      s_final_hours = (s_time_digits[0] * 10) + s_time_digits[1];
      s_final_minutes = (s_time_digits[2] * 10) + s_time_digits[3];
      set_app_state(APP_STATE_WRITING);
    } else if (s_time_cursor == 5) {
      s_time_cursor = 0;
      update_time_highlight();
    }
  } else if (s_app_state == APP_STATE_FINISHED) {
    set_app_state(APP_STATE_INPUT_END_COUNT);
  } else if (s_app_state == APP_STATE_INPUT_END_COUNT) {
    if (s_count_cursor < 6) {
      s_count_cursor++;
      update_count_highlight();
    } else if (s_count_cursor == 6) {
      s_end_count = calculate_count_digits();
      set_app_state(APP_STATE_SPRINT_SUMMARY);
    } else if (s_count_cursor == 7) {
      s_count_cursor = 0;
      update_count_highlight();
    }
  } else if (s_app_state == APP_STATE_SPRINT_SUMMARY) {
    if (s_summary_menu_index == 0) {
      s_start_count = s_end_count;
      load_digits_from_number(s_start_count);
      refresh_all_count_displays();
      set_app_state(APP_STATE_INPUT_TIME);
    } else {
      set_app_state(APP_STATE_PROJECT_HUB);
    }
  }
}

// BACK BUTTON (Left)
static void back_click_handler(ClickRecognizerRef recognizer, void *context) {
  if (s_app_state == APP_STATE_SPLASH) {
    if (s_splash_timer) {
      app_timer_cancel(s_splash_timer);
      s_splash_timer = NULL;
    }
    window_stack_pop(true);
  } else if (s_app_state == APP_STATE_PROJECT_HUB) {
    set_app_state(APP_STATE_EXITING);
  } else if (s_app_state == APP_STATE_CREATE_NAME) {
    if (s_name_cursor >= 16) {
      s_name_cursor = s_name_last_char_pos;
      update_name_highlight();
      refresh_name_preview();
    } else if (s_name_cursor > 0) {
      s_name_cursor--;
      update_name_highlight();
      refresh_name_preview();
    } else {
      set_app_state(APP_STATE_PROJECT_HUB);
    }
  } else if (s_app_state == APP_STATE_LOAD_LIST) {
    set_app_state(APP_STATE_PROJECT_HUB);
  } else if (s_app_state == APP_STATE_INPUT_COUNT) {
    if (s_count_cursor > 0) {
      s_count_cursor--;
      update_count_highlight();
    } else {
      if (s_navigated_from_load) {
        set_app_state(APP_STATE_CONFIRM_MENU);
      } else {
        set_app_state(APP_STATE_CREATE_NAME);
      }
    }
  } else if (s_app_state == APP_STATE_CONFIRM_MENU) {
    if (s_navigated_from_load) {
      set_app_state(APP_STATE_LOAD_LIST);
    } else {
      s_count_cursor = 6;
      update_count_highlight();
      set_app_state(APP_STATE_INPUT_COUNT);
    }
  } else if (s_app_state == APP_STATE_INPUT_TIME) {
    if (s_time_cursor > 0) {
      s_time_cursor--;
      update_time_highlight();
    } else {
      if (s_navigated_from_load && s_current_project_idx == s_last_sprinted_idx) {
        set_app_state(APP_STATE_PROJECT_HUB);
      } else {
        set_app_state(APP_STATE_CONFIRM_MENU);
      }
    }
  } else if (s_app_state == APP_STATE_WRITING) {
    set_app_state(APP_STATE_INPUT_TIME);
  } else if (s_app_state == APP_STATE_FINISHED) {
    set_app_state(APP_STATE_INPUT_TIME);
  } else if (s_app_state == APP_STATE_INPUT_END_COUNT) {
    if (s_count_cursor > 0) {
      s_count_cursor--;
      update_count_highlight();
    } else {
      set_app_state(APP_STATE_FINISHED);
    }
  } else if (s_app_state == APP_STATE_SPRINT_SUMMARY) {
    set_app_state(APP_STATE_PROJECT_HUB);
  } else if (s_app_state == APP_STATE_EXITING) {
    if (s_exit_timer) {
      app_timer_cancel(s_exit_timer);
      s_exit_timer = NULL;
    }
    window_stack_pop(true);
  }
}

static void click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP, up_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
  window_single_click_subscribe(BUTTON_ID_BACK, back_click_handler);
}

static void main_window_load(Window *window) {
  window_set_background_color(window, COLOR_BG);
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  int mid_x = bounds.size.w / 2;

  // ================= 0. SPLASH SCREEN (BUBBLE CANVAS) =================
  s_splash_canvas_layer = layer_create(bounds);
  layer_set_update_proc(s_splash_canvas_layer, splash_canvas_update_proc);
  layer_add_child(window_layer, s_splash_canvas_layer);

  // ================= 1. PROJECT HUB =================
  s_hub_title_layer = create_themed_text_layer(GRect(0, 15, bounds.size.w, 26), FONT_KEY_GOTHIC_18_BOLD, GTextAlignmentCenter);
  text_layer_set_text(s_hub_title_layer, "PROJECT HUB");
  layer_add_child(window_layer, text_layer_get_layer(s_hub_title_layer));

  s_hub_highlight_layer = layer_create(GRect(mid_x - 60, 62, 120, 30));
  layer_set_update_proc(s_hub_highlight_layer, box_draw_proc);
  layer_add_child(window_layer, s_hub_highlight_layer);

  s_hub_opt_resume_layer = create_themed_text_layer(GRect(0, 42, bounds.size.w, 34), FONT_KEY_GOTHIC_14_BOLD, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_hub_opt_resume_layer));

  s_hub_opt1_layer = create_themed_text_layer(GRect(0, 66, bounds.size.w, 24), FONT_KEY_GOTHIC_18_BOLD, GTextAlignmentCenter);
  text_layer_set_text(s_hub_opt1_layer, "Create New");
  layer_add_child(window_layer, text_layer_get_layer(s_hub_opt1_layer));

  s_hub_opt2_layer = create_themed_text_layer(GRect(0, 108, bounds.size.w, 24), FONT_KEY_GOTHIC_18_BOLD, GTextAlignmentCenter);
  text_layer_set_text(s_hub_opt2_layer, "Load Existing");
  layer_add_child(window_layer, text_layer_get_layer(s_hub_opt2_layer));

  // ================= 2. CREATE NAME =================
  s_name_title_layer = create_themed_text_layer(GRect(0, 10, bounds.size.w, 22), FONT_KEY_GOTHIC_18_BOLD, GTextAlignmentCenter);
  text_layer_set_text(s_name_title_layer, "NAME PROJECT");
  layer_add_child(window_layer, text_layer_get_layer(s_name_title_layer));

  s_name_preview_layer = create_themed_text_layer(GRect(0, 36, bounds.size.w, 32), FONT_KEY_GOTHIC_18_BOLD, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_name_preview_layer));

  s_name_highlight_layer = layer_create(GRect(mid_x - 60, 70, 120, 24));
  layer_set_update_proc(s_name_highlight_layer, box_draw_proc);
  layer_add_child(window_layer, s_name_highlight_layer);

  s_name_cur_char_layer = create_themed_text_layer(GRect(0, 72, bounds.size.w, 22), FONT_KEY_GOTHIC_18_BOLD, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_name_cur_char_layer));

  s_name_set_layer = create_themed_text_layer(GRect(0, 102, bounds.size.w, 22), FONT_KEY_GOTHIC_18_BOLD, GTextAlignmentCenter);
  text_layer_set_text(s_name_set_layer, "SET");
  layer_add_child(window_layer, text_layer_get_layer(s_name_set_layer));

  s_name_amend_layer = create_themed_text_layer(GRect(0, 132, bounds.size.w, 22), FONT_KEY_GOTHIC_18_BOLD, GTextAlignmentCenter);
  text_layer_set_text(s_name_amend_layer, "AMEND");
  layer_add_child(window_layer, text_layer_get_layer(s_name_amend_layer));

  // ================= 3. LOAD EXISTING =================
  s_load_title_layer = create_themed_text_layer(GRect(0, 12, bounds.size.w, 26), FONT_KEY_GOTHIC_18_BOLD, GTextAlignmentCenter);
  text_layer_set_text(s_load_title_layer, "SELECT PROJECT");
  layer_add_child(window_layer, text_layer_get_layer(s_load_title_layer));

  s_load_highlight_layer = layer_create(GRect(mid_x - 66, 48, 132, 28));
  layer_set_update_proc(s_load_highlight_layer, box_draw_proc);
  layer_add_child(window_layer, s_load_highlight_layer);

  for (int i = 0; i < 3; i++) {
    s_load_items_layers[i] = create_themed_text_layer(GRect(0, 50 + (i * 32), bounds.size.w, 26), FONT_KEY_GOTHIC_18_BOLD, GTextAlignmentCenter);
    layer_add_child(window_layer, text_layer_get_layer(s_load_items_layers[i]));
  }

  s_load_empty_layer = create_themed_text_layer(GRect(0, 50, bounds.size.w, 44), FONT_KEY_GOTHIC_18_BOLD, GTextAlignmentCenter);
  text_layer_set_text(s_load_empty_layer, "Nothing to\nsee here!");
  layer_add_child(window_layer, text_layer_get_layer(s_load_empty_layer));

  s_load_back_layer = create_themed_text_layer(GRect(0, 112, bounds.size.w, 24), FONT_KEY_GOTHIC_18_BOLD, GTextAlignmentCenter);
  text_layer_set_text(s_load_back_layer, "Back");
  layer_add_child(window_layer, text_layer_get_layer(s_load_back_layer));

  // ================= 4. WORD COUNT SCREEN =================
  s_count_title_layer = create_themed_text_layer(GRect(0, 15, bounds.size.w, 26), FONT_KEY_GOTHIC_18_BOLD, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_count_title_layer));

  int count_start_x = (bounds.size.w - 96) / 2;
  s_count_highlight_layer = layer_create(GRect(count_start_x, 52, 14, 28));
  layer_set_update_proc(s_count_highlight_layer, box_draw_proc);
  layer_add_child(window_layer, s_count_highlight_layer);

  for (int i = 0; i < 6; i++) {
    s_count_digit_layers[i] = create_themed_text_layer(GRect(count_start_x + (i * 16), 52, 14, 28), FONT_KEY_GOTHIC_24_BOLD, GTextAlignmentCenter);
    refresh_count_display(i);
    layer_add_child(window_layer, text_layer_get_layer(s_count_digit_layers[i]));
  }

  s_count_set_layer = create_themed_text_layer(GRect(0, 94, bounds.size.w, 22), FONT_KEY_GOTHIC_18_BOLD, GTextAlignmentCenter);
  text_layer_set_text(s_count_set_layer, "SET");
  layer_add_child(window_layer, text_layer_get_layer(s_count_set_layer));

  s_count_amend_layer = create_themed_text_layer(GRect(0, 124, bounds.size.w, 22), FONT_KEY_GOTHIC_18_BOLD, GTextAlignmentCenter);
  text_layer_set_text(s_count_amend_layer, "AMEND");
  layer_add_child(window_layer, text_layer_get_layer(s_count_amend_layer));

  // ================= 5. CONFIRM MENU SCREEN =================
  s_locked_title_layer = create_themed_text_layer(GRect(0, 10, bounds.size.w, 26), FONT_KEY_GOTHIC_18_BOLD, GTextAlignmentCenter);
  text_layer_set_text(s_locked_title_layer, "START COUNT SET");
  layer_add_child(window_layer, text_layer_get_layer(s_locked_title_layer));

  s_count_display_layer = create_themed_text_layer(GRect(0, 36, bounds.size.w, 30), FONT_KEY_GOTHIC_24_BOLD, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_count_display_layer));

  s_menu_highlight_layer = layer_create(GRect(mid_x - 64, 88, 128, 26));
  layer_set_update_proc(s_menu_highlight_layer, box_draw_proc);
  layer_add_child(window_layer, s_menu_highlight_layer);

  s_menu_option1_layer = create_themed_text_layer(GRect(0, 90, bounds.size.w, 24), FONT_KEY_GOTHIC_18_BOLD, GTextAlignmentCenter);
  text_layer_set_text(s_menu_option1_layer, "Ready to Write");
  layer_add_child(window_layer, text_layer_get_layer(s_menu_option1_layer));

  s_menu_option2_layer = create_themed_text_layer(GRect(0, 118, bounds.size.w, 40), FONT_KEY_GOTHIC_14, GTextAlignmentCenter);
  text_layer_set_text(s_menu_option2_layer, "Change Starting\nWord Count");
  layer_add_child(window_layer, text_layer_get_layer(s_menu_option2_layer));

  // ================= 6. SPRINT TIME =================
  s_time_title_layer = create_themed_text_layer(GRect(0, 15, bounds.size.w, 26), FONT_KEY_GOTHIC_18_BOLD, GTextAlignmentCenter);
  text_layer_set_text(s_time_title_layer, "SET SPRINT TIME");
  layer_add_child(window_layer, text_layer_get_layer(s_time_title_layer));

  int time_mid_x = bounds.size.w / 2;
  s_time_highlight_layer = layer_create(GRect(time_mid_x - 40, 52, 16, 28));
  layer_set_update_proc(s_time_highlight_layer, box_draw_proc);
  layer_add_child(window_layer, s_time_highlight_layer);

  s_time_digit_layers[0] = create_themed_text_layer(GRect(time_mid_x - 40, 52, 16, 28), FONT_KEY_GOTHIC_24_BOLD, GTextAlignmentCenter);
  s_time_digit_layers[1] = create_themed_text_layer(GRect(time_mid_x - 22, 52, 16, 28), FONT_KEY_GOTHIC_24_BOLD, GTextAlignmentCenter);

  s_time_colon_layer = create_themed_text_layer(GRect(time_mid_x - 6, 50, 12, 28), FONT_KEY_GOTHIC_24_BOLD, GTextAlignmentCenter);
  text_layer_set_text(s_time_colon_layer, ":");
  layer_add_child(window_layer, text_layer_get_layer(s_time_colon_layer));

  s_time_digit_layers[2] = create_themed_text_layer(GRect(time_mid_x + 6, 52, 16, 28), FONT_KEY_GOTHIC_24_BOLD, GTextAlignmentCenter);
  s_time_digit_layers[3] = create_themed_text_layer(GRect(time_mid_x + 24, 52, 16, 28), FONT_KEY_GOTHIC_24_BOLD, GTextAlignmentCenter);

  for (int i = 0; i < 4; i++) {
    refresh_time_display(i);
    layer_add_child(window_layer, text_layer_get_layer(s_time_digit_layers[i]));
  }

  s_time_set_layer = create_themed_text_layer(GRect(0, 94, bounds.size.w, 22), FONT_KEY_GOTHIC_18_BOLD, GTextAlignmentCenter);
  text_layer_set_text(s_time_set_layer, "SET");
  layer_add_child(window_layer, text_layer_get_layer(s_time_set_layer));

  s_time_amend_layer = create_themed_text_layer(GRect(0, 124, bounds.size.w, 22), FONT_KEY_GOTHIC_18_BOLD, GTextAlignmentCenter);
  text_layer_set_text(s_time_amend_layer, "AMEND");
  layer_add_child(window_layer, text_layer_get_layer(s_time_amend_layer));

  // ================= 7 & 8. WRITING / FINISHED SCREEN =================
  // Monkey side-profile animation canvas (top half)
  s_monkey_anim_layer = layer_create(GRect(0, 10, bounds.size.w, 62));
  layer_set_update_proc(s_monkey_anim_layer, monkey_anim_update_proc);
  layer_add_child(window_layer, s_monkey_anim_layer);

  // Sprint status text (bottom half)
  s_writing_layer = create_themed_text_layer(GRect(0, 76, bounds.size.w, 88), FONT_KEY_GOTHIC_18_BOLD, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_writing_layer));

  // ================= 9. SPRINT RESULTS / SUMMARY MENU =================
  s_summary_stats_layer = create_themed_text_layer(GRect(0, 8, bounds.size.w, 68), FONT_KEY_GOTHIC_18_BOLD, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_summary_stats_layer));

  s_summary_highlight_layer = layer_create(GRect(mid_x - 68, 82, 136, 38));
  layer_set_update_proc(s_summary_highlight_layer, box_draw_proc);
  layer_add_child(window_layer, s_summary_highlight_layer);

  s_summary_opt1_layer = create_themed_text_layer(GRect(0, 82, bounds.size.w, 38), FONT_KEY_GOTHIC_14_BOLD, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_summary_opt1_layer));

  s_summary_opt2_layer = create_themed_text_layer(GRect(0, 126, bounds.size.w, 30), FONT_KEY_GOTHIC_14_BOLD, GTextAlignmentCenter);
  text_layer_set_text(s_summary_opt2_layer, "Return to Main Menu");
  layer_add_child(window_layer, text_layer_get_layer(s_summary_opt2_layer));

  load_projects();
  set_app_state(APP_STATE_SPLASH);
}

static void main_window_unload(Window *window) {
  if (s_splash_timer) {
    app_timer_cancel(s_splash_timer);
    s_splash_timer = NULL;
  }
  if (s_sprint_timer) {
    app_timer_cancel(s_sprint_timer);
    s_sprint_timer = NULL;
  }
  if (s_exit_timer) {
    app_timer_cancel(s_exit_timer);
    s_exit_timer = NULL;
  }

  layer_destroy(s_splash_canvas_layer);

  text_layer_destroy(s_hub_title_layer);
  text_layer_destroy(s_hub_opt_resume_layer);
  text_layer_destroy(s_hub_opt1_layer);
  text_layer_destroy(s_hub_opt2_layer);
  layer_destroy(s_hub_highlight_layer);

  text_layer_destroy(s_name_title_layer);
  text_layer_destroy(s_name_preview_layer);
  text_layer_destroy(s_name_cur_char_layer);
  text_layer_destroy(s_name_set_layer);
  text_layer_destroy(s_name_amend_layer);
  layer_destroy(s_name_highlight_layer);

  text_layer_destroy(s_load_title_layer);
  for (int i = 0; i < 3; i++) {
    text_layer_destroy(s_load_items_layers[i]);
  }
  text_layer_destroy(s_load_empty_layer);
  text_layer_destroy(s_load_back_layer);
  layer_destroy(s_load_highlight_layer);

  text_layer_destroy(s_count_title_layer);
  for (int i = 0; i < 6; i++) {
    text_layer_destroy(s_count_digit_layers[i]);
  }
  text_layer_destroy(s_count_set_layer);
  text_layer_destroy(s_count_amend_layer);
  layer_destroy(s_count_highlight_layer);

  text_layer_destroy(s_locked_title_layer);
  text_layer_destroy(s_count_display_layer);
  text_layer_destroy(s_menu_option1_layer);
  text_layer_destroy(s_menu_option2_layer);
  layer_destroy(s_menu_highlight_layer);

  text_layer_destroy(s_time_title_layer);
  for (int i = 0; i < 4; i++) {
    text_layer_destroy(s_time_digit_layers[i]);
  }
  text_layer_destroy(s_time_colon_layer);
  text_layer_destroy(s_time_set_layer);
  text_layer_destroy(s_time_amend_layer);
  layer_destroy(s_time_highlight_layer);

  text_layer_destroy(s_writing_layer);
  layer_destroy(s_monkey_anim_layer);

  text_layer_destroy(s_summary_stats_layer);
  text_layer_destroy(s_summary_opt1_layer);
  text_layer_destroy(s_summary_opt2_layer);
  layer_destroy(s_summary_highlight_layer);
}

static void init(void) {
  s_main_window = window_create();
  window_set_click_config_provider(s_main_window, click_config_provider);
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = main_window_load,
    .unload = main_window_unload
  });
  window_stack_push(s_main_window, true);
}

static void deinit(void) {
  window_destroy(s_main_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}