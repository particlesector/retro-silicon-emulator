// SPDX-License-Identifier: Apache-2.0
#include "display.hpp"
#include <stdexcept>
#include <string>

namespace rse::desktop {
void require_sdl(bool success) {if (!success) throw std::runtime_error(SDL_GetError());}
Display::Display(SDL_Window* window) {
    renderer_.reset(SDL_CreateRenderer(window,"vulkan"));
    if (!renderer_) throw std::runtime_error(std::string("Vulkan renderer unavailable: ")+SDL_GetError()+". Install a Vulkan-capable graphics driver.");
    if (std::string_view(backend())!="vulkan") throw std::runtime_error("Vulkan renderer was not selected");
    // Audio consumption paces the machine; presentation must not block on vsync.
    (void)SDL_SetRenderVSync(renderer_.get(),0);
    texture_.reset(SDL_CreateTexture(renderer_.get(),SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STREAMING,256,240));
    require_sdl(bool(texture_));require_sdl(SDL_SetTextureScaleMode(texture_.get(),SDL_SCALEMODE_NEAREST));
}
void Display::draw(const video::PpuFrame* frame,bool verify) {
    auto* r=renderer_.get();int width{},height{};
    require_sdl(SDL_SetRenderLogicalPresentation(r,0,0,SDL_LOGICAL_PRESENTATION_DISABLED));
    require_sdl(SDL_GetRenderOutputSize(r,&width,&height));
    if (width<=0 || height<=0) return;
    require_sdl(SDL_SetRenderDrawColor(r,0,0,0,255));require_sdl(SDL_RenderClear(r));
    const auto view=viewport(width,height);
    if (frame) {
        const auto image=pixels(*frame);
        static_assert(sizeof(Rgb)==4);
        require_sdl(SDL_UpdateTexture(texture_.get(),nullptr,image.data(),256*sizeof(Rgb)));
        const SDL_FRect destination{float(view.x),float(view.y),float(view.w),float(view.h)};
        require_sdl(SDL_RenderTexture(r,texture_.get(),nullptr,&destination));
        if (verify) {
            std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> raw(SDL_RenderReadPixels(r,nullptr),SDL_DestroySurface);
            require_sdl(bool(raw));
            std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> copy(SDL_ConvertSurface(raw.get(),SDL_PIXELFORMAT_RGBA32),SDL_DestroySurface);
            require_sdl(bool(copy));
            auto at=[&](int x,int y) {return *reinterpret_cast<const Rgb*>(static_cast<const std::uint8_t*>(copy->pixels)+y*copy->pitch+x*4);};
            // Sample texture interiors away from scaling boundaries and check
            // every letterbox edge. Alpha is backend-specific; compare RGB.
            for (int y:{view.h/4,view.h/2,view.h*3/4}) for (int x:{view.w/4,view.w/2,view.w*3/4}) {
                const auto expected=image[std::min(239,y*240/view.h)*256+std::min(255,x*256/view.w)];
                const auto actual=at(view.x+x,view.y+y);
                if (actual.r!=expected.r || actual.g!=expected.g || actual.b!=expected.b)
                    throw std::runtime_error("Vulkan texture readback mismatch");
            }
            for (auto [x,y]:{std::pair{0,0},std::pair{width-1,height-1}}) {
                if (x<view.x || y<view.y || x>=view.x+view.w || y>=view.y+view.h) {
                    const auto c=at(x,y);if (c.r || c.g || c.b) throw std::runtime_error("Vulkan letterbox mismatch");
                }
            }
            ++readbacks_;
        }
    } else {
        require_sdl(SDL_SetRenderLogicalPresentation(r,480,360,SDL_LOGICAL_PRESENTATION_LETTERBOX));
        require_sdl(SDL_SetRenderDrawColor(r,150,190,255,255));
        require_sdl(SDL_RenderDebugText(r,32,40,"Retro Silicon Emulator"));
        require_sdl(SDL_SetRenderDrawColor(r,210,220,235,255));
        constexpr const char* lines[]{"Open a cartridge to start.","","O / Ctrl+O    Open cartridge",
            "Or drop a .nes file here.","","X / Z         A / B","Arrow keys    Directions",
            "Enter         Start","Right Shift   Select","","F5 Reset      Esc Exit"};
        float y=76;for (auto* line:lines) {require_sdl(SDL_RenderDebugText(r,32,y,line));y+=18;}
    }
    require_sdl(SDL_RenderPresent(r));
}
} // namespace rse::desktop
