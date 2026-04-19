#ifndef GAME_LOGIC_H
#define GAME_LOGIC_H

#include <pebble.h>

#define GET_RANK(card) ((card) & 0x0F)
#define GET_SUIT(card) (((card) >> 4) & 0x03)
#define MAKE_CARD(suit, rank) (((suit) << 4) | (rank))

typedef uint8_t Card;

typedef struct {
  uint32_t cards_mask;
  uint8_t score_benefit;
} MeldCandidate;

typedef struct {
  Card cards[11];
  uint8_t count;
  uint32_t meld_mask;
  uint32_t layoff_mask;
} Hand;

typedef struct {
  Card deck[52];
  uint8_t deck_count;
  Card discard_pile[52];
  uint8_t discard_count;
  Hand player_hand;
  Hand cpu_hand;
  uint16_t player_match_score;
  uint16_t cpu_match_score;
  uint8_t last_win_type;
  bool is_player_turn;
} GameState;

void game_logic_init_state(GameState *state);
uint8_t game_logic_calculate_best_deadwood(Hand *hand);
void game_logic_apply_layoff(Hand *knocker_hand, Hand *opponent_hand);
void game_logic_finalize_round(GameState *state, bool player_knocked, bool is_draw);
uint8_t game_logic_get_card_score(Card card);
uint8_t game_logic_cpu_choose_card_to_discard(Hand *hand);
void game_logic_discard_to_pile(GameState *state, Hand *hand, uint8_t card_index);

#endif