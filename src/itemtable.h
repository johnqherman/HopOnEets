// blueprint name -> idle .anim path, so the opponent's locked-in build draws
// as the real items (translucent). Derived from each Data/Objects/<bp>.lua's
// animation set; an unmapped blueprint falls back to the box marker.
#pragma once
#include <cstring>

struct ItemAnim {
  const char *blueprint;
  const char *anim; // DATA:-rooted .anim path
};

static const ItemAnim ITEM_ANIMS[] = {
    {"bob", "DATA:Animations/Bob/bob_blink.anim"},
    {"butterfly", "DATA:Animations/butterfly/butterfly.anim"},
    {"cart_ejection", "DATA:Animations/Cart/cart_ejection.anim"},
    {"cart_tnt", "DATA:Animations/Cart/cart_tnt.anim"},
    {"choco_cloud", "DATA:Animations/Choco Cloud/choco_cloud_idle.anim"},
    {"chocolate_chip",
     "DATA:Animations/Chocolate Chip/Chocolate_Chip_idle.anim"},
    {"choco_pump", "DATA:Animations/Choco Pump/choco_pump_idle.anim"},
    {"emotion_platform_angry",
     "DATA:Animations/Platforms/emotion_platform_angry_inactive.anim"},
    {"emotion_platform_happy",
     "DATA:Animations/Platforms/emotion_platform_happy_inactive.anim"},
    {"emotion_platform_scared",
     "DATA:Animations/Platforms/emotion_platform_scared_inactive.anim"},
    {"firefly_blue", "DATA:Animations/firefly/firefly_blue.anim"},
    {"firefly_green", "DATA:Animations/firefly/firefly_green.anim"},
    {"firefly_red", "DATA:Animations/firefly/firefly_red.anim"},
    {"giant_marshmallow",
     "DATA:Animations/Giant Marshmallow/giant_marshmallow_blink.anim"},
    {"giant_marshmallow_balloon", "DATA:Animations/Giant Marshmallow/"
                                  "giant_marshmallow_balloon_inflated.anim"},
    {"giant_marshmallow_exploding",
     "DATA:Animations/Giant Marshmallow/giant_marshmallow_exploding_upset_and_"
     "mumble.anim"},
    {"ginseng", "DATA:Animations/ginseng/ginseng.anim"},
    {"ginseng_factory",
     "DATA:Animations/ginseng factory/ginseng_factory_idle.anim"},
    {"ginseng_light", "DATA:Animations/ginseng light/ginseng_light_idle.anim"},
    {"ginseng_static_light",
     "DATA:Animations/static light/static_light_idle.anim"},
    {"marsh_bud_angry",
     "DATA:Animations/Marshmallow Bud/marsh_bud_angry_look_blink.anim"},
    {"marsh_bud_gravity",
     "DATA:Animations/Marshmallow Bud/marsh_bud_gravity_look_blink.anim"},
    {"marsh_bud_happy",
     "DATA:Animations/Marshmallow Bud/marsh_bud_happy_normal.anim"},
    {"marsh_bud_scared",
     "DATA:Animations/Marshmallow Bud/marsh_bud_scared_look_blink.anim"},
    {"marshomech", "DATA:Animations/Marshomech/marshomech_idle.anim"},
    {"merch", "DATA:Animations/Merch/merch_happy_idle.anim"},
    {"merch_merch", "DATA:Animations/Merch/Merch Merch/merch_merch_ball.anim"},
    {"reflector", "DATA:Animations/Reflector/reflector_normal.anim"},
    {"sneezy_sow", "DATA:Animations/sneezy sow/sneezy_sow_idle.anim"},
    {"star", "DATA:Animations/Star/starman_curls.anim"},
    {"superbounce", "DATA:Animations/Star/starman_faces.anim"},
    {"superpig", "DATA:Animations/SuperPig/super_pig_flying.anim"},
    {"whale", "DATA:Animations/Whale/whale_idle.anim"},
};

// nullptr when unmapped (platform/goal/eets and anything a newer game ships)
static const char *item_anim_path(const char *blueprint) {
  if (!blueprint || !blueprint[0])
    return nullptr;
  for (auto &e : ITEM_ANIMS)
    if (strcmp(e.blueprint, blueprint) == 0)
      return e.anim;
  return nullptr;
}
