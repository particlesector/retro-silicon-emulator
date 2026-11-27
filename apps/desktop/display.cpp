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
    // Default to refresh-paced presentation and honor SDL's VSync override.
    require_sdl(SDL_SetRenderVSync(renderer_.get(),
        SDL_GetHintBoolean(SDL_HINT_RENDER_VSYNC,true) ? 1 : 0));

}
void Display::draw(const Frame* frame,bool verify,const WorkspaceOverlay* workspace,bool gba) {
    auto* r=renderer_.get();int width{},height{};
    require_sdl(SDL_SetRenderLogicalPresentation(r,0,0,SDL_LOGICAL_PRESENTATION_DISABLED));
    require_sdl(SDL_GetRenderOutputSize(r,&width,&height));
    if (width<=0 || height<=0) return;
    require_sdl(SDL_SetRenderDrawColor(r,0,0,0,255));require_sdl(SDL_RenderClear(r));
    const auto panel=workspace ? std::min(width/2,420) : 0;
    const auto view=viewport(width-panel,height,frame?frame->format.display_aspect:session::Rational{4,3});
    if(view.w<=0 || view.h<=0){require_sdl(SDL_RenderPresent(r));return;}
    if (frame && frame->number && !frame->pixels.empty()) {
        const auto fw=frame->format.width,fh=frame->format.height;
        if(!texture_ || texture_width_!=fw || texture_height_!=fh) {
            texture_.reset(SDL_CreateTexture(r,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STREAMING,int(fw),int(fh)));
            require_sdl(bool(texture_));require_sdl(SDL_SetTextureScaleMode(texture_.get(),SDL_SCALEMODE_NEAREST));
            texture_width_=fw;texture_height_=fh;
        }
        const auto& image=frame->pixels;
        static_assert(sizeof(Rgb)==4);
        require_sdl(SDL_UpdateTexture(texture_.get(),nullptr,image.data(),int(fw*sizeof(Rgb))));
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
                const auto expected=image[std::min(int(fh)-1,y*int(fh)/view.h)*fw+std::min(int(fw)-1,x*int(fw)/view.w)];
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
    } else if(!workspace) {
        require_sdl(SDL_SetRenderLogicalPresentation(r,480,360,SDL_LOGICAL_PRESENTATION_LETTERBOX));
        require_sdl(SDL_SetRenderDrawColor(r,150,190,255,255));
        require_sdl(SDL_RenderDebugText(r,32,40,"Retro Silicon Emulator"));
        require_sdl(SDL_SetRenderDrawColor(r,210,220,235,255));
        const char* lines[]{"Open a cartridge to start.","","O / Ctrl+O    Open cartridge",
            gba ? "Or drop a .gba file here." : "Or drop a .nes file here.","","X / Z         A / B","Arrow keys    Directions",
            "Enter         Start","Right Shift   Select","","F1 AI workspace   F5 Reset   Esc Exit"};
        if(gba) {
            lines[9]="Q / W         L / R";
            lines[10]="F12 BIOS   P Pause   F5 Reset   Esc Exit";
        }
        float y=76;for (auto* line:lines) {require_sdl(SDL_RenderDebugText(r,32,y,line));y+=18;}
    }
    if(workspace && panel>0) {
        const SDL_FRect background{float(width-panel),0,float(panel),float(height)};
        require_sdl(SDL_SetRenderDrawColor(r,19,26,40,255));
        require_sdl(SDL_RenderFillRect(r,&background));
        const float x=background.x+12;
        float y=16;
        const auto columns=std::size_t(std::max(0,panel-24)/8);
        for(std::size_t i=0;i<workspace->lines.size() && y+10<height;++i) {
            if(i==0) require_sdl(SDL_SetRenderDrawColor(r,136,201,255,255));
            else require_sdl(SDL_SetRenderDrawColor(r,224,231,244,255));
            const auto line=workspace->lines[i].substr(0,columns);
            if(!line.empty()) require_sdl(SDL_RenderDebugText(r,x,y,line.c_str()));
            y+=18;
        }
        if(const auto* preview=workspace->preview;preview && preview->number && y+48<height && panel>32) {
            if(!preview_texture_) {
                preview_texture_.reset(SDL_CreateTexture(r,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STREAMING,256,240));
                require_sdl(bool(preview_texture_));
                require_sdl(SDL_SetTextureScaleMode(preview_texture_.get(),SDL_SCALEMODE_NEAREST));
            }
            require_sdl(SDL_UpdateTexture(preview_texture_.get(),nullptr,preview->pixels.data(),256*sizeof(Rgb)));
            const auto fitted=viewport(panel-24,height-int(y)-12,{4,3});
            const SDL_FRect destination{x+fitted.x,y+fitted.y,float(fitted.w),float(fitted.h)};
            if(fitted.w>0 && fitted.h>0)
                require_sdl(SDL_RenderTexture(r,preview_texture_.get(),nullptr,&destination));
        }
    }
    require_sdl(SDL_RenderPresent(r));
}
} // namespace rse::desktop
