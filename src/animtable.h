// anim base-name <-> small-int id map for the pos wire token. Sender (match.h) sends the id,
// receiver (net.h) maps back; an unknown name/id falls back to the raw string (lossless).
// APPEND-ONLY: the index IS the wire value, so only ever add at the end, never reorder/remove.
#pragma once
#include <cstring>

// names as Object_GetCurrentAnimName reports them
static const char *const ANIM_TABLE[] = {
    "eets_happy_walk",  "eets_happy_jump",  "eets_happy_fall",
    "eets_happy_squat", "eets_happy_eat",   "eets_happy_land",
    "eets_angry_walk",  "eets_angry_jump",  "eets_angry_fall",
    "eets_angry_squat", "eets_angry_eat",   "eets_angry_land",
    "eets_scared_walk", "eets_scared_jump", "eets_scared_fall",
    "eets_scared_squat", "eets_scared_eat", "eets_scared_land",
    // _nm ("no mouth") variants
    "eets_happy_walk_nm",  "eets_happy_jump_nm",  "eets_happy_fall_nm",
    "eets_happy_squat_nm", "eets_happy_eat_nm",   "eets_happy_land_nm",
    "eets_angry_walk_nm",  "eets_angry_jump_nm",  "eets_angry_fall_nm",
    "eets_angry_squat_nm", "eets_angry_eat_nm",   "eets_angry_land_nm",
    "eets_scared_walk_nm", "eets_scared_jump_nm", "eets_scared_fall_nm",
    "eets_scared_squat_nm", "eets_scared_eat_nm", "eets_scared_land_nm",
    // emote one-shots
    "eets_emote_happy",        "eets_emote_angry_first", "eets_emote_angry_2",
    "eets_emote_angry_3",      "eets_emote_angry_4",     "eets_emote_angry_5",
    "eets_emote_scared_first", "eets_emote_scared_2",    "eets_emote_scared_3",
};
static constexpr int ANIM_TABLE_N =
    (int)(sizeof(ANIM_TABLE) / sizeof(ANIM_TABLE[0]));

// base name -> id (>=0), or -1 when absent (caller sends the raw name)
static int anim_name_to_id(const char *name) {
  if (!name || !name[0])
    return -1;
  for (int i = 0; i < ANIM_TABLE_N; i++)
    if (strcmp(ANIM_TABLE[i], name) == 0)
      return i;
  return -1;
}
// id -> base name, or nullptr when out of range (caller keeps the raw token)
static const char *anim_id_to_name(int id) {
  return (id >= 0 && id < ANIM_TABLE_N) ? ANIM_TABLE[id] : nullptr;
}
// all-digits = a compact id token (not a raw name or "-")
static bool anim_token_is_id(const char *tok) {
  return tok && tok[0] && strspn(tok, "0123456789") == strlen(tok);
}
