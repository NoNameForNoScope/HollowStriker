#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <iostream>
#include <vector>
#include "../sim/world.hpp"
#include "../render/raycaster.hpp"
#include "../render/hud.hpp"
#include "../sim/camera.hpp"
#include "../sim/true_body.hpp"

const int WIDTH = 800;
const int HEIGHT = 600;

int main(int argc, char** argv) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;

    SDL_Window* window = SDL_CreateWindow("BODYCAM-MINI Cinematic Edition", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WIDTH, HEIGHT, SDL_WINDOW_SHOWN);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, WIDTH, HEIGHT);
    
    SDL_SetRelativeMouseMode(SDL_TRUE);

    bodycam::World world(101010);
    world.map.width = 15; world.map.height = 15;
    world.map.grid = {
        '1','1','1','1','1','1','1','1','1','1','1','1','1','1','1',
        '1','.','.','.','.','.','1','.','.','.','.','.','.','.','1',
        '1','.','.','.','.','.','1','.','.','.','.','.','.','.','1',
        '1','.','.','1','1','.','1','.','.','1','1','1','.','.','1',
        '1','.','.','.','.','.','.','.','.','.','.','.','.','.','1',
        '1','.','.','.','.','.','.','.','.','.','.','.','.','.','1',
        '1','1','1','1','.','.','.','.','.','.','.','1','1','1','1',
        '1','.','.','.','.','.','1','1','1','.','.','.','.','.','1',
        '1','.','.','.','.','.','1','1','1','.','.','.','.','.','1',
        '1','.','.','.','.','.','.','.','.','.','.','.','.','.','1',
        '1','.','.','.','.','.','.','.','.','.','.','.','.','.','1',
        '1','.','.','1','1','1','.','.','1','1','.','.','.','.','1',
        '1','.','.','.','.','.','.','.','1','.','.','.','.','.','1',
        '1','.','.','.','.','.','.','.','1','.','.','.','.','.','1',
        '1','1','1','1','1','1','1','1','1','1','1','1','1','1','1'
    };
    
    bodycam::Agent player;
    player.id = 0; player.x = 2.5; player.y = 2.5; player.facing = 0; player.speed = 0; player.faction = bodycam::Faction::Blue;
    world.agents.push_back(player);

    for(int i=1; i<=4; i++) {
        bodycam::Agent bot;
        bot.id = i; bot.x = 2.0 + i*2.0; bot.y = 12.0; bot.facing = 0; bot.speed = 0; 
        bot.faction = bodycam::Faction::Red; bot.state = bodycam::FsmState::Move;
        world.agents.push_back(bot);
    }

    bodycam::CameraState inertialCamera;
    bodycam::TrueBodyTransform trueBody;

    // Activam setlecinile cinematic bodycam din start pentru realism vizual maxim
    bool fisheye_on = true;
    bool vignette_on = true;
    bool grain_on = true;
    
    bool running = true;
    uint32_t last_time = SDL_GetTicks();
    double accumulator = 0.0;

    std::vector<uint32_t> pixels(WIDTH * HEIGHT);
    bodycam::AgentControl input;

    while (running) {
        uint32_t current_time = SDL_GetTicks();
        double frame_time = (current_time - last_time) / 1000.0;
        last_time = current_time;
        if (frame_time > 0.25) frame_time = 0.25;
        accumulator += frame_time;

        input.desiredTurn = 0.0;
        input.fire = false;
        
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            else if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_ESCAPE) running = false;
                if (e.key.keysym.sym == SDLK_F2) fisheye_on = !fisheye_on;
                if (e.key.keysym.sym == SDLK_F3) vignette_on = !vignette_on;
                if (e.key.keysym.sym == SDLK_F4) grain_on = !grain_on;
            }
            else if (e.type == SDL_MOUSEMOTION) {
                input.desiredTurn = e.motion.xrel * 0.003; 
            }
            else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                input.fire = true;
            }
        }

        const Uint8* state = SDL_GetKeyboardState(NULL);
        double fwd = 0.0, str = 0.0;
        double speed_mult = state[SDL_SCANCODE_LSHIFT] ? world.params.v_sprint : world.params.v_walk;
        
        if (state[SDL_SCANCODE_W]) fwd += speed_mult;
        if (state[SDL_SCANCODE_S]) fwd -= speed_mult * 0.5;
        if (state[SDL_SCANCODE_A]) str -= speed_mult * 0.8;
        if (state[SDL_SCANCODE_D]) str += speed_mult * 0.8;
        
        input.forwardSpeed = fwd;
        input.strafeSpeed = str;

        bool draw_flash = false;
        if (input.fire && world.agents[0].fire_timer <= 0 && world.agents[0].ammo > 0) {
            draw_flash = true;
        }

        while (accumulator >= world.dt) {
            world.step(&input);
            accumulator -= world.dt;
        }

        bodycam::update_camera_inertia(inertialCamera, world.agents[0].x, world.agents[0].y, world.agents[0].facing, world.agents[0].speed, world.dt);
        trueBody.update(world.agents[0].speed, world.dt, inertialCamera.breathingOffset, input.desiredTurn);

        bodycam::Agent renderCam = world.agents[0];
        renderCam.x = inertialCamera.x;
        renderCam.y = inertialCamera.y;
        renderCam.facing = inertialCamera.smoothFacing;
        renderCam.bob_phase = inertialCamera.swayPhase;

        // Randare lume 3D texturata procedural (podea, pere?i, inamici)
        bodycam::render_world(world, 0, pixels.data(), WIDTH, HEIGHT);
        
        // Randare HUD tactic
        bodycam::draw_hud(pixels.data(), WIDTH, HEIGHT, world.agents[0], world.current_time);

        if (draw_flash) {
            for(int i = 0; i < WIDTH * HEIGHT; i++) pixels[i] = 0xFFFFAA00;
        }

        // Aplicare suitei avansate de post-procesare bodycam (Distorsiune, Abera?ie cromatica, Vignetare, Grain)
        bodycam::apply_bodycam_pipeline(pixels.data(), WIDTH, HEIGHT, current_time, fisheye_on, vignette_on, grain_on);

        SDL_UpdateTexture(texture, NULL, pixels.data(), WIDTH * 4);
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);
    }

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
