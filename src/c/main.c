#include <pebble.h>
#include "game_logic.h"

typedef enum {
  STEP_TITLE,
  STEP_PLAYER_DRAW,
  STEP_PLAYER_DISCARD,
  STEP_PLAYER_KNOCK_CHECK,
  STEP_SHOW_HANDS,
  STEP_CPU_THINKING,
  STEP_ANIMATING_DRAW,
  STEP_RESULT
} GameStep;

#if defined(PBL_PLATFORM_EMERY)
  #define CARD_W 25
  #define CARD_H 38
  #define H_SPACING 17
  #define P_HAND_X 3
  #define P_HAND_Y 154
  #define C_HAND_X 3
  #define C_HAND_Y 26
  #define DECK_Y 96
#elif defined(PBL_PLATFORM_GABBRO)
  #define CARD_W 25
  #define CARD_H 38
  #define H_SPACING 17
  #define P_HAND_X 32
  #define P_HAND_Y 177
  #define C_HAND_X 32
  #define C_HAND_Y 45
  #define DECK_Y 111
#elif defined(PBL_PLATFORM_CHALK)
  #define CARD_W 20
  #define CARD_H 30
  #define H_SPACING 12
  #define P_HAND_X 14
  #define P_HAND_Y 108
  #define C_HAND_X 32
  #define C_HAND_Y 22
  #define DECK_Y 61
#else
  // Aplite, Basalt, Diorite
  #define CARD_W 20
  #define CARD_H 30
  #define H_SPACING 10
  #define P_HAND_X 12
  #define P_HAND_Y 110
  #define C_HAND_X 12
  #define C_HAND_Y 21
  #define DECK_Y 65
#endif

#define TITLE_Y_OFFSET 10
#define RESULT_Y_OFFSET 26

#if defined(PBL_PLATFORM_EMERY) || defined(PBL_PLATFORM_GABBRO)
  #define CLOCK_H 24
#else
  #define CLOCK_H 20
#endif

static GBitmap *s_suit_bmps[4];
static TextLayer *s_time_layer;
static GColor s_highlight_bg_color = PBL_IF_COLOR_ELSE(GColorOxfordBlue, GColorBlack);
static GColor s_highlight_text_color = GColorWhite;
static GBitmap *s_rank_10_bmps[2];
static Window *s_main_window;
static Layer *s_canvas_layer;
static GameState s_game_state;
static uint16_t s_round_start_ps = 0;
static uint16_t s_round_start_cs = 0;
static GameStep s_current_step;
static bool s_next_round_first_is_cpu = false;
static uint8_t s_round_count = 1;
static GameStep s_anim_target_step;
static Card s_anim_card;
static GPoint s_anim_pos;
static int8_t s_selection_index;

static void redraw_callback(void *data) { if (s_canvas_layer) layer_mark_dirty(s_canvas_layer); }

static void draw_card(GContext *ctx, GRect rect, Card card, bool selected, bool hidden) {
  if (selected) rect.origin.y -= (CARD_H > 30 ? 8 : 5);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, rect, 3, GCornersAll);
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_draw_round_rect(ctx, rect, 3);
  if (hidden) {
    graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(GColorCobaltBlue, GColorLightGray));
    graphics_fill_rect(ctx, grect_inset(rect, GEdgeInsets(2)), 1, GCornersAll);
    return;
  }
  uint8_t rank = GET_RANK(card); uint8_t suit = GET_SUIT(card);
  int draw_x = rect.origin.x + 2;
  graphics_context_set_compositing_mode(ctx, GCompOpSet);
  graphics_draw_bitmap_in_rect(ctx, s_suit_bmps[suit], GRect(draw_x, rect.origin.y + 2, gbitmap_get_bounds(s_suit_bmps[suit]).size.w, gbitmap_get_bounds(s_suit_bmps[suit]).size.h));
  if (rank == 10) {
    bool is_red = (suit == 1 || suit == 2);
    GBitmap *bmp = s_rank_10_bmps[is_red ? 1 : 0];
    if (bmp) {
      graphics_draw_bitmap_in_rect(ctx, bmp, GRect(draw_x, rect.origin.y + (CARD_H > 30 ? 15 : 12), gbitmap_get_bounds(bmp).size.w, gbitmap_get_bounds(bmp).size.h));
    }
  } else {
    char s_rank[3];
    if (rank == 1) strcpy(s_rank, "A"); else if (rank == 11) strcpy(s_rank, "J");
    else if (rank == 12) strcpy(s_rank, "Q"); else if (rank == 13) strcpy(s_rank, "K");
    else snprintf(s_rank, sizeof(s_rank), "%d", rank);
    graphics_context_set_text_color(ctx, (suit == 1 || suit == 2) ? GColorRed : GColorBlack);
    graphics_draw_text(ctx, s_rank, fonts_get_system_font(CARD_H > 30 ? FONT_KEY_GOTHIC_18_BOLD : FONT_KEY_GOTHIC_14_BOLD), GRect(draw_x, rect.origin.y + (CARD_H > 30 ? 8 : 7), rect.size.w - 4, 20), GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
  }
}

static void update_time() {
  time_t temp = time(NULL);
  struct tm *tick_time = localtime(&temp);
  static char s_buffer[8];
  strftime(s_buffer, sizeof(s_buffer), "%H:%M", tick_time);
  text_layer_set_text(s_time_layer, s_buffer);
}

static void tick_handler(struct tm *t, TimeUnits u) {
  update_time();
}

static void canvas_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, GColorCobaltBlue);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
	
  if (s_current_step == STEP_TITLE) {
    graphics_draw_text(ctx, "GIN RUMMY\n100pts Match\nSELECT to Start", fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD), GRect(0, 10 + 10, bounds.size.w, 100), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    graphics_draw_text(ctx, "UP/DOWN: Select Card\nSELECT: Discard / OK", fonts_get_system_font(FONT_KEY_GOTHIC_18), GRect(0, bounds.size.h / 2 + 20, bounds.size.w, 50), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    return;
  }

	uint8_t p_dw = game_logic_calculate_best_deadwood(&s_game_state.player_hand);
	
  if (s_current_step == STEP_RESULT) {
    static char res[96]; char res_t[32];
    uint16_t p_gain = s_game_state.player_match_score - s_round_start_ps;
    uint16_t c_gain = s_game_state.cpu_match_score - s_round_start_cs;
    bool player_won = (p_gain > c_gain);
    switch(s_game_state.last_win_type) {
      case 1: strcpy(res_t, "GIN!!"); break;
      case 2: strcpy(res_t, "BIG GIN!!"); break;
      case 3: snprintf(res_t, 32, "UNDERCUT! %s", player_won ? "YOU WIN" : "CPU WIN"); break;
			case 4: strcpy(res_t, "DRAW (STOCK OUT)"); break;
      default: strcpy(res_t, player_won ? "WIN" : "LOSE"); break;
    }
    snprintf(res, sizeof(res), "%s\n%d pts\nScore: P%d-C%d\nSELECT: Next", 
             res_t, player_won ? p_gain : c_gain, 
             s_game_state.player_match_score, s_game_state.cpu_match_score);
    if (s_game_state.player_match_score >= 100 || s_game_state.cpu_match_score >= 100) {
			snprintf(res, sizeof(res), "MATCH OVER\nWinner: %s\nP%d - C%d\n(%d rounds)", (s_game_state.player_match_score >= 100 ? "PLAYER" : "CPU"), s_game_state.player_match_score, s_game_state.cpu_match_score, s_round_count);
		}
    GRect res_rect = GRect(0, 26, bounds.size.w, bounds.size.h - 26);
    graphics_draw_text(ctx, res, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD), res_rect, GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    return;
  }

  for (uint8_t i = 0; i < s_game_state.cpu_hand.count; i++) {
    bool is_layoff = (s_game_state.cpu_hand.layoff_mask & (1 << i));
    GRect r = GRect(C_HAND_X + (i * H_SPACING), C_HAND_Y + (is_layoff ? 5 : 0), CARD_W, CARD_H);
    draw_card(ctx, r, s_game_state.cpu_hand.cards[i], false, (s_current_step != STEP_SHOW_HANDS && !is_layoff));
  }
  draw_card(ctx, GRect(bounds.size.w/2 - CARD_W - 5, DECK_Y, CARD_W, CARD_H), 0, (s_current_step == STEP_PLAYER_DRAW && s_selection_index == 0), true);
  if (s_game_state.discard_count > 0) draw_card(ctx, GRect(bounds.size.w/2 + 5, DECK_Y, CARD_W, CARD_H), s_game_state.discard_pile[s_game_state.discard_count - 1], (s_current_step == STEP_PLAYER_DRAW && s_selection_index == 1), false);
	
  for (uint8_t i = 0; i < s_game_state.player_hand.count; i++) {
    bool sel = (s_current_step == STEP_PLAYER_DISCARD && s_selection_index == i);
    bool is_layoff = (s_game_state.player_hand.layoff_mask & (1 << i));
    GRect r = GRect(P_HAND_X + (i * H_SPACING), P_HAND_Y, CARD_W, CARD_H);
    draw_card(ctx, r, s_game_state.player_hand.cards[i], (sel || is_layoff), false);
    if (s_game_state.player_hand.meld_mask & (1 << i)) {
      graphics_context_set_fill_color(ctx, GColorWhite);
      graphics_fill_rect(ctx, GRect(r.origin.x + (CARD_W/5), r.origin.y - (sel ? 13 : 8), 2, 2), 0, GCornerNone);
    }
  }
  static char s_status_buf[48];
  snprintf(s_status_buf, sizeof(s_status_buf), "Dw:%d S:P%d C%d", 
           p_dw, s_game_state.player_match_score, s_game_state.cpu_match_score);
	graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, s_status_buf, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), GRect(0, P_HAND_Y + CARD_H + 2, bounds.size.w, 16), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

  if (s_current_step == STEP_PLAYER_KNOCK_CHECK || (s_current_step == STEP_SHOW_HANDS && p_dw == 0 && s_game_state.player_hand.count == 11)) {

    bool is_big_gin = (p_dw == 0 && s_game_state.player_hand.count == 11);
    int16_t pop_w = bounds.size.w * (is_big_gin ? 8 : 7) / 10;
    int16_t pop_h = is_big_gin ? 40 : 80;
    int16_t pop_y = is_big_gin ? (C_HAND_Y + CARD_H + 2) : C_HAND_Y;
    
    GRect pop = GRect((bounds.size.w - pop_w) / 2, pop_y, pop_w, pop_h);
    graphics_context_set_fill_color(ctx, GColorWhite); graphics_fill_rect(ctx, pop, 4, GCornersAll);
    graphics_context_set_stroke_color(ctx, GColorBlack); graphics_draw_round_rect(ctx, pop, 4);
    graphics_context_set_text_color(ctx, GColorBlack);
    if (is_big_gin) {
      graphics_draw_text(ctx, "BIG GIN!!", fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD), GRect(pop.origin.x, pop.origin.y + 1, pop.size.w, 24), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    } else {
      graphics_draw_text(ctx, "KNOCK?", fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), GRect(pop.origin.x, pop.origin.y + 5, pop.size.w, 20), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
      static char sy[16], sn[16]; snprintf(sy, 16, "%s YES", (s_selection_index == 0) ? ">" : " "); snprintf(sn, 16, "%s NO", (s_selection_index == 1) ? ">" : " ");
      graphics_draw_text(ctx, sy, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), GRect(pop.origin.x, pop.origin.y + 30, pop.size.w, 20), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
      graphics_draw_text(ctx, sn, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), GRect(pop.origin.x, pop.origin.y + 52, pop.size.w, 20), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    }
  }
  if (s_current_step == STEP_ANIMATING_DRAW) draw_card(ctx, GRect(s_anim_pos.x, s_anim_pos.y, CARD_W, CARD_H), s_anim_card, true, (s_anim_target_step == STEP_CPU_THINKING));
}

static void cpu_discard_timer_cb(void *data) {
  if (s_current_step != STEP_CPU_THINKING) return;
  game_logic_discard_to_pile(&s_game_state, &s_game_state.cpu_hand, game_logic_cpu_choose_card_to_discard(&s_game_state.cpu_hand));
  if (game_logic_calculate_best_deadwood(&s_game_state.cpu_hand) <= 10) { game_logic_finalize_round(&s_game_state, false, false); s_current_step = STEP_SHOW_HANDS; }
  else { s_current_step = STEP_PLAYER_DRAW; s_selection_index = -1; }
  redraw_callback(NULL);
}

static void anim_finish_timer_cb(void *data) {
  if (s_anim_target_step == STEP_PLAYER_DISCARD) {
    s_game_state.player_hand.cards[s_game_state.player_hand.count++] = s_anim_card;
    uint8_t p_dw = game_logic_calculate_best_deadwood(&s_game_state.player_hand);
    if (p_dw == 0 && s_game_state.player_hand.count == 11) { game_logic_finalize_round(&s_game_state, true, false); s_current_step = STEP_SHOW_HANDS; }
    else s_current_step = STEP_PLAYER_DISCARD;
		} else {
    s_game_state.cpu_hand.cards[s_game_state.cpu_hand.count++] = s_anim_card;
    uint8_t c_dw = game_logic_calculate_best_deadwood(&s_game_state.cpu_hand);
    if (c_dw == 0 && s_game_state.cpu_hand.count == 11) { game_logic_finalize_round(&s_game_state, false, false); s_current_step = STEP_SHOW_HANDS; }
    else { s_current_step = STEP_CPU_THINKING; app_timer_register(600, cpu_discard_timer_cb, NULL); }
  }
  redraw_callback(NULL);
}

static void anim_mid_timer_cb(void *data) {
  GRect b = layer_get_bounds(s_canvas_layer);
  int mid_x = (s_anim_pos.x < b.size.w/2) ? (b.size.w/2 - CARD_W) : (b.size.w/2 + CARD_W);
  s_anim_pos = (s_anim_target_step == STEP_PLAYER_DISCARD) ? GPoint(mid_x, P_HAND_Y - 20) : GPoint(mid_x, C_HAND_Y + 20);
  redraw_callback(NULL); app_timer_register(200, anim_finish_timer_cb, NULL);
}

static void start_draw_animation(Card card, bool from_deck, GameStep target_step) {
  GRect b = layer_get_bounds(s_canvas_layer); s_anim_card = card; s_anim_target_step = target_step;
  s_anim_pos = from_deck ? GPoint(b.size.w/2 - CARD_W - 5, DECK_Y) : GPoint(b.size.w/2 + 5, DECK_Y);
  s_current_step = STEP_ANIMATING_DRAW; redraw_callback(NULL); app_timer_register(200, anim_mid_timer_cb, NULL);
}

static void cpu_timer_callback(void *data) {
  Card drawn; bool from_deck = true;
  if (s_game_state.discard_count > 0 && GET_RANK(s_game_state.discard_pile[s_game_state.discard_count-1]) <= 5) { drawn = s_game_state.discard_pile[--s_game_state.discard_count]; from_deck = false; }
  else {
    if (s_game_state.deck_count <= 2) {
      game_logic_finalize_round(&s_game_state, false, true);
      s_current_step = STEP_SHOW_HANDS; redraw_callback(NULL); return;
    }
    drawn = s_game_state.deck[--s_game_state.deck_count];
  }
  start_draw_animation(drawn, from_deck, STEP_CPU_THINKING);
}

static void select_handler(ClickRecognizerRef r, void *context) {
  if (s_current_step == STEP_ANIMATING_DRAW) return;
  if (s_current_step == STEP_TITLE || s_current_step == STEP_RESULT) {
    bool match_over = (s_game_state.player_match_score >= 100 || s_game_state.cpu_match_score >= 100);
    if (s_current_step == STEP_RESULT && match_over) {
      s_current_step = STEP_TITLE;
      redraw_callback(NULL);
      return; }
    uint16_t ps = s_game_state.player_match_score, cs = s_game_state.cpu_match_score;
    game_logic_init_state(&s_game_state);
    if (s_current_step == STEP_RESULT) {
      s_game_state.player_match_score = ps; s_game_state.cpu_match_score = cs;
      s_next_round_first_is_cpu = !s_next_round_first_is_cpu;
      s_round_count++;
    } else {
      s_game_state.player_match_score = 0; s_game_state.cpu_match_score = 0;
      s_next_round_first_is_cpu = false;
      s_round_count = 1;
    }
    s_round_start_ps = s_game_state.player_match_score;
    s_round_start_cs = s_game_state.cpu_match_score;
    s_current_step = s_next_round_first_is_cpu ? STEP_CPU_THINKING : STEP_PLAYER_DRAW;
    s_selection_index = -1;
    if (s_current_step == STEP_CPU_THINKING) {
      app_timer_register(800, cpu_timer_callback, NULL);
    }
  } else if (s_current_step == STEP_PLAYER_DRAW) {
		if (s_selection_index < 0) return;
    Card drawn; bool from_d = (s_selection_index == 0);
    if (from_d) {
      if (s_game_state.deck_count <= 2) {
        game_logic_finalize_round(&s_game_state, false, true);
        s_current_step = STEP_SHOW_HANDS; redraw_callback(NULL); return;
      }
      drawn = s_game_state.deck[--s_game_state.deck_count];
    }
    else drawn = s_game_state.discard_pile[--s_game_state.discard_count];
    start_draw_animation(drawn, from_d, STEP_PLAYER_DISCARD);
  } else if (s_current_step == STEP_PLAYER_DISCARD) {
		if (s_selection_index < 0) return;
    game_logic_discard_to_pile(&s_game_state, &s_game_state.player_hand, s_selection_index);
    uint8_t p_dw = game_logic_calculate_best_deadwood(&s_game_state.player_hand);
    if (p_dw <= 10) {
      s_current_step = STEP_PLAYER_KNOCK_CHECK;
      s_selection_index = 1;
    } else {
      s_current_step = STEP_CPU_THINKING;
      s_selection_index = 0;
      app_timer_register(800, cpu_timer_callback, NULL);
    }
  } else if (s_current_step == STEP_PLAYER_KNOCK_CHECK) {
    if (s_selection_index == 0) { game_logic_finalize_round(&s_game_state, true, false); s_current_step = STEP_SHOW_HANDS; }
    else { s_current_step = STEP_CPU_THINKING; app_timer_register(800, cpu_timer_callback, NULL); }
    s_selection_index = 0;
  } else if (s_current_step == STEP_SHOW_HANDS) s_current_step = STEP_RESULT;
  redraw_callback(NULL);
}

static void up_handler(ClickRecognizerRef r, void *c) {
  if (s_current_step == STEP_ANIMATING_DRAW) return;
  static uint32_t last_ms = 0;
  uint16_t ms_part;
  time_t sec_part = time_ms(NULL, &ms_part);
  uint32_t now_ms = (uint32_t)sec_part * 1000 + ms_part;
  if (now_ms - last_ms < 50) return;
  last_ms = now_ms;
  if (s_current_step == STEP_PLAYER_KNOCK_CHECK) s_selection_index = 0;
  else if (s_current_step == STEP_PLAYER_DRAW) {
    if (s_selection_index == -1) s_selection_index = 0;
    else if (s_game_state.discard_count > 0) s_selection_index = !s_selection_index;
  }
  else if (s_current_step == STEP_PLAYER_DISCARD) s_selection_index = (s_selection_index <= 0) ? s_game_state.player_hand.count - 1 : s_selection_index - 1;
  redraw_callback(NULL);
}

static void down_handler(ClickRecognizerRef r, void *c) {
  if (s_current_step == STEP_ANIMATING_DRAW) return;
  static uint32_t last_ms = 0;
  uint16_t ms_part;
  time_t sec_part = time_ms(NULL, &ms_part);
  uint32_t now_ms = (uint32_t)sec_part * 1000 + ms_part;
  if (now_ms - last_ms < 50) return;
  last_ms = now_ms;
  if (s_current_step == STEP_PLAYER_KNOCK_CHECK) s_selection_index = 1;
  else if (s_current_step == STEP_PLAYER_DRAW) {
    if (s_selection_index == -1) s_selection_index = (s_game_state.discard_count > 0) ? 1 : 0;
    else if (s_game_state.discard_count > 0) s_selection_index = !s_selection_index;
  }
  else if (s_current_step == STEP_PLAYER_DISCARD) s_selection_index = (s_selection_index >= s_game_state.player_hand.count - 1) ? 0 : s_selection_index + 1;
  redraw_callback(NULL);
}

static void click_config_provider(void *c) {
  window_single_click_subscribe(BUTTON_ID_SELECT, select_handler);
  window_single_repeating_click_subscribe(BUTTON_ID_UP, 150, up_handler);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 150, down_handler);
}
static void main_window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);
  s_canvas_layer = layer_create(bounds);
  layer_set_update_proc(s_canvas_layer, canvas_update_proc);
  layer_add_child(root, s_canvas_layer);
  s_time_layer = text_layer_create(GRect(0, 0, bounds.size.w, CLOCK_H));
  text_layer_set_background_color(s_time_layer, s_highlight_bg_color);
  text_layer_set_text_color(s_time_layer, s_highlight_text_color);
  text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);
  text_layer_set_font(s_time_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  layer_add_child(root, text_layer_get_layer(s_time_layer));
  update_time();
 }
static void main_window_unload(Window *window) {
  layer_destroy(s_canvas_layer);
	text_layer_destroy(s_time_layer);
}
static void init() {
  srand(time(NULL));
  uint32_t suit_res_ids[] = { RESOURCE_ID_IMAGE_SUIT_S, RESOURCE_ID_IMAGE_SUIT_H, RESOURCE_ID_IMAGE_SUIT_D, RESOURCE_ID_IMAGE_SUIT_C };
  for (int i = 0; i < 4; i++) s_suit_bmps[i] = gbitmap_create_with_resource(suit_res_ids[i]);
  s_rank_10_bmps[0] = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_RANK_10_BLACK);
  s_rank_10_bmps[1] = PBL_IF_COLOR_ELSE(gbitmap_create_with_resource(RESOURCE_ID_IMAGE_RANK_10_RED), gbitmap_create_with_resource(RESOURCE_ID_IMAGE_RANK_10_BLACK));
  s_main_window = window_create();
  window_set_click_config_provider(s_main_window, click_config_provider);
  window_set_window_handlers(s_main_window, (WindowHandlers) { .load = main_window_load, .unload = main_window_unload });
  window_stack_push(s_main_window, true);
	tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
}
static void deinit() {
  tick_timer_service_unsubscribe();
  window_destroy(s_main_window);
  for (int i = 0; i < 4; i++) gbitmap_destroy(s_suit_bmps[i]);
	for (int i = 0; i < 2; i++) gbitmap_destroy(s_rank_10_bmps[i]);
}
int main(void) { init(); app_event_loop(); deinit(); }