#include "game_logic.h"
#include <stdlib.h>
#include <string.h>

uint8_t game_logic_get_card_score(Card card) {
  uint8_t rank = GET_RANK(card);
  return (rank > 10) ? 10 : rank;
}

void game_logic_init_state(GameState *state) {
  state->deck_count = 0;
  for (uint8_t s = 0; s < 4; s++)
    for (uint8_t r = 1; r <= 13; r++)
      state->deck[state->deck_count++] = MAKE_CARD(s, r);
  for (int i = 51; i > 0; i--) {
    int j = rand() % (i + 1);
    Card temp = state->deck[i];
    state->deck[i] = state->deck[j];
    state->deck[j] = temp;
  }
  state->player_hand.count = state->cpu_hand.count = 0;
  state->player_hand.meld_mask = state->player_hand.layoff_mask = 0;
  state->cpu_hand.meld_mask = state->cpu_hand.layoff_mask = 0;
  for (uint8_t i = 0; i < 10; i++) {
    state->player_hand.cards[state->player_hand.count++] = state->deck[--state->deck_count];
    state->cpu_hand.cards[state->cpu_hand.count++] = state->deck[--state->deck_count];
  }
  state->discard_count = 0;
  state->is_player_turn = true;
}

static uint8_t calculate_benefit(Hand *hand, uint32_t mask) {
  uint8_t benefit = 0;
  for (uint8_t i = 0; i < hand->count; i++) {
    if (mask & (1 << i)) benefit += game_logic_get_card_score(hand->cards[i]);
  }
  return benefit;
}

static uint8_t find_sets(Hand *hand, MeldCandidate *cands, uint8_t max) {
  uint8_t count = 0;
  for (uint8_t r = 1; r <= 13; r++) {
    uint8_t indices[4];
    uint8_t found = 0;
    for (uint8_t i = 0; i < hand->count; i++) {
      if (GET_RANK(hand->cards[i]) == r) indices[found++] = i;
    }
    
    if (found == 3) {
      if (count < max) {
        uint32_t m = (1 << indices[0]) | (1 << indices[1]) | (1 << indices[2]);
        cands[count].cards_mask = m;
        cands[count++].score_benefit = calculate_benefit(hand, m);
      }
    } else if (found == 4) {
      uint32_t full_mask = (1 << indices[0]) | (1 << indices[1]) | (1 << indices[2]) | (1 << indices[3]);
      if (count < max) {
        cands[count].cards_mask = full_mask;
        cands[count++].score_benefit = calculate_benefit(hand, full_mask);
      }
      for (uint8_t i = 0; i < 4; i++) {
        if (count < max) {
          uint32_t sub_mask = full_mask & ~(1 << indices[i]);
          cands[count].cards_mask = sub_mask;
          cands[count++].score_benefit = calculate_benefit(hand, sub_mask);
        }
      }
    }
  }
  return count;
}

static uint8_t find_runs(Hand *hand, MeldCandidate *cands, uint8_t max) {
  uint8_t count = 0;
  for (uint8_t s = 0; s < 4; s++) {
    for (uint8_t r_start = 1; r_start <= 11; r_start++) {
      for (uint8_t len = 3; len <= 11; len++) {
        if (r_start + len - 1 > 13) break;
        uint32_t current_mask = 0;
        bool complete = true;
        for (uint8_t r = r_start; r < r_start + len; r++) {
          int8_t found_idx = -1;
          for (uint8_t h = 0; h < hand->count; h++) {
            if (GET_SUIT(hand->cards[h]) == s && GET_RANK(hand->cards[h]) == r) {
              found_idx = h; break;
            }
          }
          if (found_idx == -1) { complete = false; break; }
          current_mask |= (1 << found_idx);
        }
        if (complete && count < max) {
          cands[count].cards_mask = current_mask;
          cands[count++].score_benefit = calculate_benefit(hand, current_mask);
        }
      }
    }
  }
  return count;
}

static uint32_t s_best_mask;
static uint8_t s_max_benefit;

static void find_best_recursive(MeldCandidate *cands, uint8_t cand_count, uint8_t start, uint32_t used, uint8_t benefit) {
  if (benefit > s_max_benefit) { s_max_benefit = benefit; s_best_mask = used; }
  for (uint8_t i = start; i < cand_count; i++) {
    if (!(used & cands[i].cards_mask)) find_best_recursive(cands, cand_count, i + 1, used | cands[i].cards_mask, benefit + cands[i].score_benefit);
  }
}

uint8_t game_logic_calculate_best_deadwood(Hand *hand) {
  if (hand->count == 0) return 0;

  uint8_t total_score = 0;
  for (uint8_t i = 0; i < hand->count; i++) {
    total_score += game_logic_get_card_score(hand->cards[i]);
  }

  MeldCandidate cands[64];
  uint8_t cand_count = 0;
  cand_count = find_sets(hand, cands, 32); 
  cand_count += find_runs(hand, cands + cand_count, 64 - cand_count);
  
  s_best_mask = 0; 
  s_max_benefit = 0;
  find_best_recursive(cands, cand_count, 0, 0, 0);

  Card meld_part[11];
  Card dw_part[11];
  uint8_t m_idx = 0;
  uint8_t d_idx = 0;

  for (uint8_t i = 0; i < hand->count; i++) {
    if (s_best_mask & (1 << i)) {
      meld_part[m_idx++] = hand->cards[i];
    } else {
      dw_part[d_idx++] = hand->cards[i];
    }
  }

  if (m_idx > 1) {
    for (uint8_t i = 0; i < m_idx - 1; i++) {
      for (uint8_t j = 0; j < m_idx - i - 1; j++) {
        uint16_t v1 = (GET_RANK(meld_part[j]) << 4) | GET_SUIT(meld_part[j]);
        uint16_t v2 = (GET_RANK(meld_part[j+1]) << 4) | GET_SUIT(meld_part[j+1]);
        if (v1 > v2) { Card t = meld_part[j]; meld_part[j] = meld_part[j+1]; meld_part[j+1] = t; }
      }
    }
  }
  if (d_idx > 1) {
    for (uint8_t i = 0; i < d_idx - 1; i++) {
      for (uint8_t j = 0; j < d_idx - i - 1; j++) {
        uint16_t v1 = (GET_RANK(dw_part[j]) << 4) | GET_SUIT(dw_part[j]);
        uint16_t v2 = (GET_RANK(dw_part[j+1]) << 4) | GET_SUIT(dw_part[j+1]);
        if (v1 > v2) { Card t = dw_part[j]; dw_part[j] = dw_part[j+1]; dw_part[j+1] = t; }
      }
    }
  }

  for (uint8_t i = 0; i < m_idx; i++) {
    hand->cards[i] = meld_part[i];
  }
  for (uint8_t i = 0; i < d_idx; i++) {
    hand->cards[m_idx + i] = dw_part[i];
  }

  hand->meld_mask = (m_idx == 0) ? 0 : (uint32_t)((1U << m_idx) - 1);

  return total_score - s_max_benefit;
}

void game_logic_apply_layoff(Hand *knocker, Hand *opponent) {
  bool found;
  opponent->layoff_mask = 0;
  do {
    found = false;
    for (uint8_t i = 0; i < knocker->count; i++) {
      if (!(knocker->meld_mask & (1 << i))) continue;
      Card k = knocker->cards[i];
      for (uint8_t j = 0; j < opponent->count; j++) {
        if ((opponent->meld_mask & (1 << j)) || (opponent->layoff_mask & (1 << j))) continue;
        Card o = opponent->cards[j];
        uint8_t k_rank = GET_RANK(k);
        uint8_t k_suit = GET_SUIT(k);
        uint8_t o_rank = GET_RANK(o);
        uint8_t o_suit = GET_SUIT(o);

        bool is_set_layoff = false;
        if (k_rank == o_rank) {
          uint8_t r_count = 0;
          for (uint8_t n = 0; n < knocker->count; n++) {
            if ((knocker->meld_mask & (1 << n)) && GET_RANK(knocker->cards[n]) == k_rank) r_count++;
          }
          if (r_count >= 3) is_set_layoff = true;
        }

        bool is_run_layoff = false;
        if (!is_set_layoff && k_suit == o_suit && (o_rank == k_rank + 1 || (k_rank > 1 && o_rank == k_rank - 1))) {
          for (uint8_t n = 0; n < knocker->count; n++) {
            if (n == i || !(knocker->meld_mask & (1 << n)) || GET_SUIT(knocker->cards[n]) != k_suit) continue;
            uint8_t nr = GET_RANK(knocker->cards[n]);
            if (nr == k_rank + 1 || (k_rank > 1 && nr == k_rank - 1)) { is_run_layoff = true; break; }
          }
        }

        if (is_set_layoff || is_run_layoff) {
          opponent->layoff_mask |= (1 << j); found = true; break;
        }
      }
      if (found) break;
    }
  } while (found);
}

void game_logic_finalize_round(GameState *state, bool player_knocked, bool is_draw) {
  if (is_draw) {
    state->last_win_type = 4; // 4: Draw (Stock out)
    return;
  }
  Hand *knocker = player_knocked ? &state->player_hand : &state->cpu_hand;
  Hand *opponent = player_knocked ? &state->cpu_hand : &state->player_hand;
  uint8_t k_dw = game_logic_calculate_best_deadwood(knocker);
	(void)game_logic_calculate_best_deadwood(opponent);
  game_logic_apply_layoff(knocker, opponent);
  uint8_t o_dw = 0;
  for (uint8_t i = 0; i < opponent->count; i++) {
    if (!(opponent->meld_mask & (1 << i)) && !(opponent->layoff_mask & (1 << i))) o_dw += game_logic_get_card_score(opponent->cards[i]);
  }
  int16_t score = 0;
  if (k_dw == 0) {
    bool big = (knocker->count == 11);
    state->last_win_type = big ? 2 : 1;
    score = o_dw + (big ? 31 : 25);
  } else if (k_dw < o_dw) { 
    state->last_win_type = 0; score = o_dw - k_dw; 
  } else { 
    state->last_win_type = 3; score = (k_dw - o_dw) + 25; 
  }
  
  bool knocker_wins = (state->last_win_type < 3);
  if (player_knocked) {
    if (knocker_wins) state->player_match_score += score; else state->cpu_match_score += score;
  } else {
    if (knocker_wins) state->cpu_match_score += score; else state->player_match_score += score;
  }
}

uint8_t game_logic_cpu_choose_card_to_discard(Hand *hand) {
  uint8_t best = 0; uint8_t min_dw = 255;
  for (uint8_t i = 0; i < hand->count; i++) {
    Hand temp = *hand;
    for (uint8_t j = i; j < temp.count - 1; j++) temp.cards[j] = temp.cards[j+1];
    temp.count--;
    uint8_t dw = game_logic_calculate_best_deadwood(&temp);
    if (dw < min_dw) { min_dw = dw; best = i; }
  }
  return best;
}

void game_logic_discard_to_pile(GameState *state, Hand *hand, uint8_t idx) {
  state->discard_pile[state->discard_count++] = hand->cards[idx];
  for (uint8_t i = idx; i < hand->count - 1; i++) hand->cards[i] = hand->cards[i+1];
  hand->count--;
}