// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "presentation.hpp"
#include <SDL3/SDL.h>
#include <memory>

namespace rse::desktop {
class Display {
public:
    explicit Display(SDL_Window* window);
    void draw(const video::PpuFrame* frame,bool verify=false);
    [[nodiscard]] const char* backend() const noexcept {return SDL_GetRendererName(renderer_.get());}
    [[nodiscard]] unsigned readbacks() const noexcept {return readbacks_;}
private:
    std::unique_ptr<SDL_Renderer,decltype(&SDL_DestroyRenderer)> renderer_{nullptr,SDL_DestroyRenderer};
    std::unique_ptr<SDL_Texture,decltype(&SDL_DestroyTexture)> texture_{nullptr,SDL_DestroyTexture};
    unsigned readbacks_{};
};
void require_sdl(bool success);
} // namespace rse::desktop
