// A0 of the Android export (ADR 0012). Answers, in the log and on the screen, the questions that
// the plan depends on:
//   - can the program read a file that is in the APK (SDL_IOFromFile) and can std::ifstream?
//   - can it write where saves will go (SDL_GetPrefPath)?
//   - does a touch arrive as a mouse click, and as a finger event?
//   - what does the Back button do?
// The screen shows the answers (the background is green when the asset was read, red when not;
// the square is orange, blue after a click) so that CI can check them in a screenshot.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <fstream>
#include <string>

namespace {

constexpr int logical_width = 480;
constexpr int logical_height = 800;

bool read_asset(std::string& text) {
    size_t size = 0;
    void* data = SDL_LoadFile("data/hello.txt", &size);
    if (data == nullptr) {
        SDL_Log("SPIKE asset=fail (%s)", SDL_GetError());
        return false;
    }
    text.assign(static_cast<const char*>(data), size);
    SDL_free(data);
    SDL_Log("SPIKE asset=ok text=%s", text.c_str());
    return true;
}

void try_ifstream() {
    std::ifstream file("data/hello.txt");
    SDL_Log("SPIKE ifstream=%s", file.good() ? "ok" : "fail");
}

void try_pref_path() {
    char* path = SDL_GetPrefPath("ge", "spike");
    if (path == nullptr) {
        SDL_Log("SPIKE pref=fail (%s)", SDL_GetError());
        return;
    }
    const std::string file = std::string(path) + "save.txt";
    const std::string content = "saved";
    const bool wrote = SDL_SaveFile(file.c_str(), content.data(), content.size());
    size_t size = 0;
    void* back = SDL_LoadFile(file.c_str(), &size);
    const bool same =
        back != nullptr && std::string(static_cast<const char*>(back), size) == content;
    SDL_free(back);
    SDL_Log("SPIKE pref=%s path=%s", (wrote && same) ? "ok" : "fail", path);
    SDL_free(path);
}

} // namespace

int main(int /*argc*/, char** /*argv*/) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SPIKE init=fail (%s)", SDL_GetError());
        return 1;
    }
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    if (!SDL_CreateWindowAndRenderer("spike", logical_width, logical_height, SDL_WINDOW_FULLSCREEN,
                                     &window, &renderer)) {
        SDL_Log("SPIKE window=fail (%s)", SDL_GetError());
        return 1;
    }
    SDL_SetRenderLogicalPresentation(renderer, logical_width, logical_height,
                                     SDL_LOGICAL_PRESENTATION_LETTERBOX);
    int pixel_width = 0;
    int pixel_height = 0;
    SDL_GetWindowSizeInPixels(window, &pixel_width, &pixel_height);
    SDL_Log("SPIKE window=ok pixels=%dx%d renderer=%s", pixel_width, pixel_height,
            SDL_GetRendererName(renderer));

    std::string text;
    const bool asset_ok = read_asset(text);
    try_ifstream();
    try_pref_path();

    int clicks = 0;
    int frames = 0;
    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                SDL_Log("SPIKE quit");
                running = false;
            } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                ++clicks;
                SDL_Log("SPIKE click which=%u x=%.0f y=%.0f",
                        static_cast<unsigned>(event.button.which),
                        static_cast<double>(event.button.x), static_cast<double>(event.button.y));
            } else if (event.type == SDL_EVENT_FINGER_DOWN) {
                SDL_Log("SPIKE finger x=%.2f y=%.2f", static_cast<double>(event.tfinger.x),
                        static_cast<double>(event.tfinger.y));
            } else if (event.type == SDL_EVENT_KEY_DOWN) {
                SDL_Log("SPIKE key=%s", SDL_GetKeyName(event.key.key));
            }
        }
        if (asset_ok) {
            SDL_SetRenderDrawColor(renderer, 16, 96, 16, 255);
        } else {
            SDL_SetRenderDrawColor(renderer, 160, 16, 16, 255);
        }
        SDL_RenderClear(renderer);
        if (clicks > 0) {
            SDL_SetRenderDrawColor(renderer, 0, 0, 255, 255);
        } else {
            SDL_SetRenderDrawColor(renderer, 255, 128, 0, 255);
        }
        const SDL_FRect square{logical_width / 2.0F - 100.0F, logical_height / 2.0F - 100.0F,
                               200.0F, 200.0F};
        SDL_RenderFillRect(renderer, &square);
        SDL_RenderPresent(renderer);
        if (++frames % 120 == 0) {
            SDL_Log("SPIKE frames=%d clicks=%d", frames, clicks);
        }
        SDL_Delay(8);
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
