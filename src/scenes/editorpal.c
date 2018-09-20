/*
 * Open Surge Engine
 * editorpal.c - level editor: item palette
 * Copyright (C) 2018  Alexandre Martins <alemartf(at)gmail(dot)com>
 * http://opensurge2d.org
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "editorpal.h"
#include "../core/font.h"
#include "../core/scene.h"
#include "../core/audio.h"
#include "../core/soundfactory.h"
#include "../core/util.h"
#include "../core/video.h"
#include "../core/image.h"
#include "../core/sprite.h"
#include "../core/input.h"
#include "../entities/brick.h"


/* private data */
#define DEFAULT_ITEM_SPRITE         "SD_QUESTIONMARK"
#define CURSOR_SPRITE               "SD_ARROW"
#define ITEM_SPRITE_MAXSIZE         128
#define ITEM_BOX_SIZE               160 /* sprite size + padding */
#define ITEM_MAX_ZOOM               2.0f
#define SCROLLBAR_WIDTH             24
#define NO_ITEM                     -1
static editorpal_config_t config;
static input_t *pal_input;
static font_t *error_font;
static font_t *cursor_font;
static image_t* cursor_image;
static input_t* cursor_input;
static v2d_t cursor_position;
static image_t* background;
static const image_t** item; /* an array of images */
static int item_count;
static int selected_item;
static int scroll_y, scroll_max;
static int item_at(v2d_t position);
static void draw_item(image_t* dest, int item_number, v2d_t center);



/*
 * editorpal_init()
 * Initializes the editor palette
 */
void editorpal_init(void *config_ptr)
{
    int i;

    /* Read the config struct */
    config = *((editorpal_config_t*)config_ptr);

    /* read the items */
    if(config.type == EDITORPAL_SSOBJ) {
        item_count = config.ssobj.count;
        item = mallocx(item_count * sizeof(*item));
        for(i = 0; i < item_count; i++) {
            if(sprite_animation_exists(config.ssobj.name[i], 0))
                item[i] = sprite_get_image(sprite_get_animation(config.ssobj.name[i], 0), 0);
            else
                item[i] = sprite_get_image(sprite_get_animation(DEFAULT_ITEM_SPRITE, 0), 0);
        }
    }
    else if(config.type == EDITORPAL_BRICK) {
        item_count = config.brick.count;
        item = mallocx(item_count * sizeof(*item));
        for(i = 0; i < item_count; i++) {
            if(brick_exists(config.brick.id[i]))
                item[i] = brick_image_preview(config.brick.id[i]);
            else
                item[i] = sprite_get_image(sprite_get_animation(DEFAULT_ITEM_SPRITE, 0), 0); /* shouldn't happen */
        }
    }
    else {
        item_count = 0;
        item = NULL;
    }

    /* configure the mouse cursor */
    cursor_image = sprite_get_image(sprite_get_animation(CURSOR_SPRITE, 0), 0);
    cursor_input = input_create_mouse();
    cursor_font = font_create("default");
    cursor_position = v2d_new(0, 0);

    /* configure the background */
    background = image_create(VIDEO_SCREEN_W, VIDEO_SCREEN_H);
    image_clear(background, image_rgb(18, 18, 18));
    image_draw_trans(video_get_backbuffer(), background, 0, 0, 0.15f, IF_NONE);

    /* misc */
    scroll_y = 0;
    scroll_max = (int)((item_count - 1) / (int)((VIDEO_SCREEN_W - SCROLLBAR_WIDTH) / ITEM_BOX_SIZE)) * ITEM_BOX_SIZE - (int)(VIDEO_SCREEN_H / ITEM_BOX_SIZE) * ITEM_BOX_SIZE + ITEM_BOX_SIZE;
    selected_item = NO_ITEM;
    pal_input = input_create_user("editorpal");
    error_font = font_create("default");
    font_set_position(error_font, v2d_new(8, 8));
}



/*
 * editorpal_release()
 * Releases the editor help screen
 */
void editorpal_release()
{
    /* release stuff */
    font_destroy(error_font);
    image_destroy(background);
    font_destroy(cursor_font);
    input_destroy(cursor_input);
    input_destroy(pal_input);

    /* release the items */
    if(item != NULL)
        free(item);
}



/*
 * editorpal_update()
 * Updates the scene
 */
void editorpal_update()
{
    v2d_t cursor_font_pos;

    /* no items? */
    if(item_count == 0) {
        font_set_text(error_font, "No items found. [press ESC]");
        font_set_visible(error_font, TRUE);
    }
    else
        font_set_visible(error_font, FALSE);

    /* cursor position */
    cursor_position.x = clip(input_get_xy((inputmouse_t*)cursor_input).x, 0, VIDEO_SCREEN_W - image_width(cursor_image)/2);
    cursor_position.y = clip(input_get_xy((inputmouse_t*)cursor_input).y, 0, VIDEO_SCREEN_H - image_height(cursor_image)/2);
    cursor_font_pos.x = clip((int)cursor_position.x, 10, VIDEO_SCREEN_W - font_get_textsize(cursor_font).x - 10);
    cursor_font_pos.y = clip((int)cursor_position.y - 3 * font_get_textsize(cursor_font).y, 10, VIDEO_SCREEN_H - 10);
    font_set_position(cursor_font, cursor_font_pos);

    /* cursor text */
    if(item_at(cursor_position) >= 0) {
        font_set_visible(cursor_font, TRUE);
        if(config.type == EDITORPAL_SSOBJ)
            font_set_text(cursor_font, "%s", config.ssobj.name[item_at(cursor_position)]);
        else if(config.type == EDITORPAL_BRICK)
            font_set_text(cursor_font, "brick %d", config.brick.id[item_at(cursor_position)]);
        else
            font_set_text(cursor_font, "item %d", item_at(cursor_position));
    }
    else
        font_set_visible(cursor_font, FALSE);

    /* scrollbar */
    if(scroll_max > 0) {
        /* handling the mouse wheel */
        if(input_button_pressed(cursor_input, IB_UP))
            scroll_y = min(scroll_y + ITEM_BOX_SIZE, scroll_max);
        else if(input_button_pressed(cursor_input, IB_DOWN))
            scroll_y = max(scroll_y - ITEM_BOX_SIZE, 0);

        /* handling clicks */
        if(cursor_position.x >= VIDEO_SCREEN_W - SCROLLBAR_WIDTH) {
            if(input_button_down(cursor_input, IB_FIRE1)) {
                int yref = (scroll_max + ITEM_BOX_SIZE) * cursor_position.y / VIDEO_SCREEN_H;
                scroll_y = (yref / ITEM_BOX_SIZE) * ITEM_BOX_SIZE;
            }
        }
    }

    /* selecting an item */
    if(input_button_pressed(cursor_input, IB_FIRE1)) {
        if(0 <= (selected_item = item_at(cursor_position))) {
            scenestack_pop();
            return;       
        }
    }

    /* go back */
    if(input_button_pressed(pal_input, IB_FIRE1)) {
        selected_item = NO_ITEM;
        sound_play( soundfactory_get("return") );
        scenestack_pop();
        return;
    }
}



/*
 * editorpal_render()
 * Renders the scene
 */
void editorpal_render()
{
    v2d_t cam = v2d_new(VIDEO_SCREEN_W / 2, VIDEO_SCREEN_H / 2);
    int w = (VIDEO_SCREEN_W - SCROLLBAR_WIDTH) / ITEM_BOX_SIZE;
    int base = w * (scroll_y / ITEM_BOX_SIZE);
    int i, x, y, active_item = NO_ITEM;

    /* render the background */
    image_blit(background, video_get_backbuffer(), 0, 0, 0, 0, image_width(background), image_height(background));

    /* render the active item background */
    active_item = item_at(cursor_position);
    if(active_item >= 0) {
        x = ((active_item - base) % w) * ITEM_BOX_SIZE;
        y = ((active_item - base) / w) * ITEM_BOX_SIZE;
        image_rectfill(video_get_backbuffer(), x, y, x + ITEM_BOX_SIZE - 1, y + ITEM_BOX_SIZE - 1, image_rgb(52, 52, 52));
    }

    /* render the items */
    for(i = 0; i < item_count; i++) {
        x = ((i - base) % w) * ITEM_BOX_SIZE + ITEM_BOX_SIZE / 2;
        y = ((i - base) / w) * ITEM_BOX_SIZE + ITEM_BOX_SIZE / 2;
        draw_item(video_get_backbuffer(), i, v2d_new(x, y));
    }

    /* render the scrollbar */
    if(scroll_max > 0) {
        int num_steps = 1 + scroll_max / ITEM_BOX_SIZE;
        int curr_step = scroll_y / ITEM_BOX_SIZE;
        int ypos = VIDEO_SCREEN_H * curr_step / num_steps;
        image_rectfill(video_get_backbuffer(), VIDEO_SCREEN_W - SCROLLBAR_WIDTH, 0, VIDEO_SCREEN_W, VIDEO_SCREEN_H, image_rgb(40, 44, 52));
        image_rectfill(video_get_backbuffer(), VIDEO_SCREEN_W - SCROLLBAR_WIDTH, ypos, VIDEO_SCREEN_W, ypos + VIDEO_SCREEN_H / num_steps, image_rgb(52, 52, 52));
    }

    /* render the error message (if any) */
    font_render(error_font, cam);

    /* render the cursor */
    image_draw(cursor_image, video_get_backbuffer(), (int)cursor_position.x, (int)cursor_position.y, IF_NONE);
    font_render(cursor_font, cam);
}



/*
 * editorpal_selected_item()
 * Returns the selected item
 * and then discards it
 */
int editorpal_selected_item()
{
    int item = selected_item;
    selected_item = NO_ITEM;
    return item;
}



/* --- private --- */

/* which item is located at position? */
int item_at(v2d_t position)
{
    if(position.x / ITEM_BOX_SIZE < (VIDEO_SCREEN_W - SCROLLBAR_WIDTH) / ITEM_BOX_SIZE) {
        int w = (VIDEO_SCREEN_W - SCROLLBAR_WIDTH) / ITEM_BOX_SIZE;
        int x = position.x / ITEM_BOX_SIZE;
        int y = position.y / ITEM_BOX_SIZE;
        int base = w * (scroll_y / ITEM_BOX_SIZE);
        int id = base + x + y * w;
        return id >= 0 && id < item_count ? id : NO_ITEM;
    }
    else
        return NO_ITEM;
}

/* draws the given item centered at the specified position */
void draw_item(image_t* dest, int item_number, v2d_t center)
{
    if(item_number >= 0 && item_number < item_count) {
        const image_t* image = item[item_number];
        int width = image_width(image);
        int height = image_height(image);
        float factor = min((float)ITEM_SPRITE_MAXSIZE / max(width, height), ITEM_MAX_ZOOM);
        v2d_t scale = v2d_new(factor, factor);
        image_draw_scaled(image, dest, center.x - width * scale.x / 2, center.y - height * scale.y / 2, scale, IF_NONE);
    }
}