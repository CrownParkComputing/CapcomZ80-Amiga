/* commando_dip.h -- shared DIP-switch definitions for Commando.
 * Used by the RTG presenter and by the ArcadeLauncher so the user can edit
 * the same DIPs before the game boots.
 */
#ifndef COMMANDO_DIP_H
#define COMMANDO_DIP_H

#include "arcade_intro.h"

#define CMD_DSW0_DEFAULT 0xff
#define CMD_DSW1_DEFAULT 0x1f

static const ai_dip_opt cmd_dip_area[] = {
    {0x03,"0 FOREST 1"}, {0x01,"2 DESERT 1"}, {0x02,"4 FOREST 2"}, {0x00,"6 DESERT 2"}
};
static const ai_dip_opt cmd_dip_lives[] = {
    {0x04,"2"}, {0x0c,"3"}, {0x08,"4"}, {0x00,"5"}
};
static const ai_dip_opt cmd_dip_coin_b[] = {
    {0x00,"4C 1C"}, {0x20,"3C 1C"}, {0x10,"2C 1C"}, {0x30,"1C 1C"}
};
static const ai_dip_opt cmd_dip_coin_a[] = {
    {0x00,"2C 1C"}, {0xc0,"1C 1C"}, {0x40,"1C 2C"}, {0x80,"1C 3C"}
};
static const ai_dip_opt cmd_dip_bonus[] = {
    {0x07,"10K 50K+"}, {0x03,"10K 60K+"}, {0x05,"20K 60K+"}, {0x01,"20K 70K+"},
    {0x06,"30K 70K+"}, {0x02,"30K 80K+"}, {0x04,"40K 100K+"}, {0x00,"NONE"}
};
static const ai_dip_opt cmd_dip_demo[] = { {0x00,"OFF"}, {0x08,"ON"} };
static const ai_dip_opt cmd_dip_diff[] = { {0x10,"NORMAL"}, {0x00,"DIFFICULT"} };
static const ai_dip_opt cmd_dip_flip[] = { {0x00,"OFF"}, {0x20,"ON"} };
static const ai_dip_opt cmd_dip_cabinet[] = {
    {0x00,"UPRIGHT"}, {0x40,"UPRIGHT 2P"}, {0xc0,"COCKTAIL"}
};

#define CMD_DIP_ITEMS \
static const ai_dip_item cmd_dip_items[] = { \
    {"STARTING AREA",0,0x03,4,cmd_dip_area}, \
    {"LIVES",0,0x0c,4,cmd_dip_lives}, \
    {"COIN B",0,0x30,4,cmd_dip_coin_b}, \
    {"COIN A",0,0xc0,4,cmd_dip_coin_a}, \
    {"BONUS LIFE",1,0x07,8,cmd_dip_bonus}, \
    {"DEMO SOUNDS",1,0x08,2,cmd_dip_demo}, \
    {"DIFFICULTY",1,0x10,2,cmd_dip_diff}, \
    {"FLIP SCREEN",1,0x20,2,cmd_dip_flip}, \
    {"CABINET",1,0xc0,3,cmd_dip_cabinet} \
};

#endif
