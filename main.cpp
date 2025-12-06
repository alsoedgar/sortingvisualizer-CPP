#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <vector>
#include <iostream>
#include <ctime>
#include <algorithm>
#include <cmath>
#include <atomic>
#include <string>
#include <sstream>
#include <iomanip> // For decimal precision

// --- CONFIGURATION ---
const int WINDOW_WIDTH = 1280;
const int WINDOW_HEIGHT = 1000;
const int NUM_RECTANGLES = 150;
const int DELAY_SHUFFLE = 1;
const float TEXT_SCALE = 2.0f;
const float LINE_HEIGHT = 18.0f;

// --- DYNAMIC SPEED ---
int currentDelay = 0;

// --- AUDIO CONFIG ---
const int SAMPLE_RATE = 44100;
const float VOLUME = 0.05f;

// --- MODES ---
enum SortMode {
    BUBBLE_SORT, SELECTION_SORT, INSERTION_SORT, QUICK_SORT, MERGE_SORT
};

// --- GLOBAL VARIABLES ---
std::vector<int> data;
SortMode currentMode = BUBBLE_SORT;
bool isSorted = false;
bool isRunning = true;

// Stats
unsigned long long comparisons = 0;
unsigned long long swaps = 0;

// TIMER VARIABLES
double preciseTimeMs = 0.0;
Uint64 perfFreq = 0; // CPU Timer Frequency

// Progress Tracking
float currentMaxProgress = 0.0f;

// Shuffle State
bool isShuffling = false;
int shuffle_i = 0;

// Audio Communication
std::atomic<int> targetHeight(0);

// --- ALGORITHM STATE VARIABLES ---
int i = 0, j = 0;
int minIdx = 0;

// Quick Sort Specific
std::vector<std::pair<int, int>> qsStack;
int qs_l=0, qs_r=0, qs_i=0, qs_j=0;
bool qs_partitionMode = false;

// Merge Sort Specific
std::vector<int> ms_temp;
int ms_curr_size=1, ms_left_start=0, ms_l=0, ms_m=0, ms_r=0, ms_i=0, ms_j=0, ms_k=0;
bool ms_copying = false;

// --- HELPER: BLUE GRADIENT COLOR ---
void SetBlueGradientColor(SDL_Renderer* renderer, int value, int max_val) {
    float ratio = (float)value / max_val;
    Uint8 r = (Uint8)(30 + ratio * 100);
    Uint8 g = (Uint8)(30 + ratio * 200);
    Uint8 b = (Uint8)(150 + ratio * 105);
    SDL_SetRenderDrawColor(renderer, r, g, b, 255);
}

// --- HELPER: RENDER PROGRESS BAR ---
void RenderProgressBar(SDL_Renderer* renderer) {
    float rawProgress = 0.0f;
    if (isSorted) {
        rawProgress = 1.0f;
    } else {
        if (currentMode == BUBBLE_SORT || currentMode == SELECTION_SORT || currentMode == INSERTION_SORT) {
            rawProgress = (float)i / NUM_RECTANGLES;
        } else {
            int sortedPairs = 0;
            int totalPairs = data.size() > 1 ? data.size() - 1 : 1;
            for (size_t k = 0; k < data.size() - 1; ++k) {
                if (data[k] <= data[k+1]) sortedPairs++;
            }
            rawProgress = (float)sortedPairs / totalPairs;
        }
    }
    if (rawProgress > currentMaxProgress) currentMaxProgress = rawProgress;
    if (comparisons == 0 && !isSorted) currentMaxProgress = 0.0f;

    float barHeight = 25.0f;
    float barY = WINDOW_HEIGHT - barHeight;

    SDL_SetRenderDrawColor(renderer, 40, 40, 50, 255);
    SDL_FRect bgRect = {0, barY, (float)WINDOW_WIDTH, barHeight};
    SDL_RenderFillRect(renderer, &bgRect);

    SDL_SetRenderDrawColor(renderer, 0, 255, 100, 255);
    SDL_FRect fillRect = {0, barY, currentMaxProgress * WINDOW_WIDTH, barHeight};
    SDL_RenderFillRect(renderer, &fillRect);
}

// --- AUDIO CALLBACK ---
void SDLCALL AudioCallback(void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount) {
    if (additional_amount <= 0) return;
    std::vector<float> buffer(additional_amount / sizeof(float));
    static double phase = 0.0;

    for (size_t k = 0; k < buffer.size(); k++) {
        int h = targetHeight.load();
        double frequency = (h <= 0) ? 0.0 : (200.0 + (h * 8.0));
        buffer[k] = (frequency > 0) ? (VOLUME * std::sin(phase)) : 0.0f;
        double phaseIncrement = (2.0 * M_PI * frequency) / SAMPLE_RATE;
        phase += phaseIncrement;
        if (phase >= 2.0 * M_PI) phase -= 2.0 * M_PI;
    }
    SDL_PutAudioStreamData(stream, buffer.data(), buffer.size() * sizeof(float));
}

// --- HELPER: RENDER UI ---
void RenderUI(SDL_Renderer* renderer, std::string fullText) {
    SDL_SetRenderScale(renderer, TEXT_SCALE, TEXT_SCALE);

    float startX = 20.0f / TEXT_SCALE;
    float currentY = 20.0f / TEXT_SCALE;

    std::stringstream ss(fullText);
    std::string line;

    while (std::getline(ss, line, '\n')) {
        if (line.empty()) {
            currentY += LINE_HEIGHT / 2;
            continue;
        }

        // Shadow
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderDebugText(renderer, startX + 2, currentY + 2, line.c_str());

        // Text Color
        if (line.find(':') != std::string::npos) {
            SDL_SetRenderDrawColor(renderer, 100, 255, 255, 255);
        } else {
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        }

        SDL_RenderDebugText(renderer, startX, currentY, line.c_str());
        currentY += LINE_HEIGHT;
    }

    SDL_SetRenderScale(renderer, 1.0f, 1.0f);
}

// --- LOGIC: RESET / PREPARE ---
void PrepareForSort() {
    isSorted = false; isShuffling = false;
    comparisons = 0; swaps = 0;
    currentMaxProgress = 0.0f;

    preciseTimeMs = 0.0; // Reset accurate timer

    i = 0; j = 0;
    if (currentMode == SELECTION_SORT) { minIdx = 0; j = 1; }
    else if (currentMode == INSERTION_SORT) { i = 1; j = 1; }
    else if (currentMode == QUICK_SORT) {
        qsStack.clear(); qsStack.push_back({0, (int)data.size() - 1}); qs_partitionMode = false;
    }
    else if (currentMode == MERGE_SORT) {
        ms_curr_size = 1; ms_left_start = 0; ms_copying = false; ms_temp = data;
    }
}

void ResetSort(SortMode newMode, bool generateNewData) {
    currentMode = newMode;
    targetHeight.store(0);
    if (generateNewData) {
        data.clear();
        for (int k = 0; k < NUM_RECTANGLES; k++) data.push_back(rand() % 100 + 5);
        PrepareForSort();
    } else {
        isShuffling = true; shuffle_i = 0; isSorted = false;
        currentMaxProgress = 0.0f;
    }
}

int main(int argc, char* argv[]) {
    srand(time(NULL));
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) return 1;

    // Get CPU Frequency for accurate timing
    perfFreq = SDL_GetPerformanceFrequency();

    SDL_AudioSpec spec;
    spec.channels = 1; spec.format = SDL_AUDIO_F32; spec.freq = SAMPLE_RATE;
    SDL_AudioStream* stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, AudioCallback, nullptr);
    if (stream) SDL_ResumeAudioDevice(SDL_GetAudioStreamDevice(stream));

    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_CreateWindowAndRenderer("Algorithm Visualizer!", WINDOW_WIDTH, WINDOW_HEIGHT, 0, &window, &renderer);

    ResetSort(BUBBLE_SORT, true);

    SDL_Event event;
    while (isRunning) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) isRunning = false;
            if (event.type == SDL_EVENT_KEY_DOWN) {
                switch (event.key.key) {
                    case SDLK_1: ResetSort(BUBBLE_SORT, true); break;
                    case SDLK_2: ResetSort(SELECTION_SORT, true); break;
                    case SDLK_3: ResetSort(INSERTION_SORT, true); break;
                    case SDLK_4: ResetSort(QUICK_SORT, true); break;
                    case SDLK_5: ResetSort(MERGE_SORT, true); break;
                    case SDLK_R: ResetSort(currentMode, false); break;
                    case SDLK_ESCAPE: isRunning = false;
                    case SDLK_UP: if(currentDelay > 0) currentDelay--; break;
                    case SDLK_DOWN: currentDelay++; break;
                }
            }
        }

        // --- UPDATE LOOP ---
        int soundVal = 0;

        // 1. Shuffling Logic
        if (isShuffling) {
            if (shuffle_i < data.size()) {
                int rIdx = rand() % data.size();
                std::swap(data[shuffle_i], data[rIdx]);
                soundVal = data[shuffle_i]; shuffle_i++;
                SDL_Delay(DELAY_SHUFFLE);
            } else { PrepareForSort(); }
        }
            // 2. Sorting Logic (WITH ACCURATE TIMING)
        else if (!isSorted) {

            // --- START STOPWATCH ---
            Uint64 startTick = SDL_GetPerformanceCounter();

            if (currentMode == BUBBLE_SORT) {
                soundVal = data[j+1]; comparisons++;
                if (data[j] > data[j+1]) { std::swap(data[j], data[j+1]); swaps++; }
                j++; if (j >= data.size() - 1 - i) { j = 0; i++; if (i >= data.size() - 1) isSorted = true; }

            } else if (currentMode == SELECTION_SORT) {
                soundVal = data[j]; comparisons++;
                if (data[j] < data[minIdx]) minIdx = j;
                j++; if (j >= data.size()) { std::swap(data[i], data[minIdx]); swaps++; i++; j = i + 1; minIdx = i; if (i >= data.size() - 1) isSorted = true; }

            } else if (currentMode == INSERTION_SORT) {
                soundVal = data[j]; comparisons++;
                if (j > 0 && data[j] < data[j-1]) { std::swap(data[j], data[j-1]); swaps++; j--; }
                else { i++; j = i; if (i >= data.size()) isSorted = true; }

            } else if (currentMode == QUICK_SORT) {
                if (!qs_partitionMode) {
                    if (qsStack.empty()) { isSorted = true; } else { auto range = qsStack.back(); qsStack.pop_back(); qs_l = range.first; qs_r = range.second; qs_i = qs_l - 1; qs_j = qs_l; qs_partitionMode = true; }
                } else {
                    soundVal = data[qs_j];
                    if (qs_j < qs_r) { comparisons++; if (data[qs_j] < data[qs_r]) { qs_i++; std::swap(data[qs_i], data[qs_j]); swaps++; } qs_j++; } else { std::swap(data[qs_i + 1], data[qs_r]); swaps++; int p = qs_i + 1; if (p + 1 < qs_r) qsStack.push_back({p + 1, qs_r}); if (qs_l < p - 1) qsStack.push_back({qs_l, p - 1}); qs_partitionMode = false; }
                }

            } else if (currentMode == MERGE_SORT) {
                if (!ms_copying) {
                    if (ms_curr_size >= data.size()) { isSorted = true; }
                    else if (ms_left_start >= data.size() - 1) { ms_curr_size *= 2; ms_left_start = 0; }
                    else { ms_l = ms_left_start; ms_m = std::min(ms_left_start + ms_curr_size - 1, (int)data.size() - 1); ms_r = std::min(ms_left_start + 2 * ms_curr_size - 1, (int)data.size() - 1); ms_i = ms_l; ms_j = ms_m + 1; ms_k = ms_l; for(int x = ms_l; x <= ms_r; x++) ms_temp[x] = data[x]; ms_copying = true; }
                } else {
                    if (ms_k <= ms_r) {
                        comparisons++;
                        if (ms_i <= ms_m && (ms_j > ms_r || ms_temp[ms_i] <= ms_temp[ms_j])) { data[ms_k] = ms_temp[ms_i]; soundVal = ms_temp[ms_i]; ms_i++; }
                        else { data[ms_k] = ms_temp[ms_j]; soundVal = ms_temp[ms_j]; ms_j++; }
                        swaps++; ms_k++;
                    }
                    else { ms_copying = false; ms_left_start += 2 * ms_curr_size; }
                }
            }

            // --- STOP STOPWATCH ---
            Uint64 endTick = SDL_GetPerformanceCounter();

            // Calculate strictly the time taken for logic (nanoseconds converted to ms)
            double frameTimeMs = (double)((endTick - startTick) * 1000) / perfFreq;
            preciseTimeMs += frameTimeMs;

            // --- ARTIFICIAL DELAY (Happens AFTER stopwatch stops) ---
            if (currentMode == MERGE_SORT && ms_copying) SDL_Delay(currentDelay + 2);
            else SDL_Delay(currentDelay);
        }

        targetHeight.store(soundVal);

        // --- RENDER ---
        SDL_SetRenderDrawColor(renderer, 10, 12, 20, 255);
        SDL_RenderClear(renderer);

        RenderProgressBar(renderer);

        float barWidth = (float)WINDOW_WIDTH / data.size();
        float barBottomY = WINDOW_HEIGHT - 30.0f;

        for (int k = 0; k < data.size(); k++) {
            float h = (float)data[k] * 7.0f;
            SetBlueGradientColor(renderer, data[k], 100);

            if (isSorted) {
                SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            }
            else {
                if (currentMode == BUBBLE_SORT && k >= data.size() - i) SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
                else if (currentMode == SELECTION_SORT && k < i) SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            }

            if (isShuffling) {
                if (k == shuffle_i) SDL_SetRenderDrawColor(renderer, 255, 50, 50, 255);
            }
            else if (!isSorted) {
                if (currentMode == BUBBLE_SORT) { if(k==j || k==j+1) SDL_SetRenderDrawColor(renderer, 255, 50, 50, 255); }
                else if (currentMode == SELECTION_SORT) { if(k==j) SDL_SetRenderDrawColor(renderer, 255, 50, 50, 255); if(k==minIdx) SDL_SetRenderDrawColor(renderer, 255, 0, 255, 255); }
                else if (currentMode == INSERTION_SORT) { if(k==j) SDL_SetRenderDrawColor(renderer, 255, 50, 50, 255); }
                else if (currentMode == QUICK_SORT) { if(k==qs_j) SDL_SetRenderDrawColor(renderer, 255, 50, 50, 255); if(k==qs_r) SDL_SetRenderDrawColor(renderer, 255, 0, 255, 255); }
                else if (currentMode == MERGE_SORT) { if(ms_copying && k == ms_k - 1) SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255); }
            }

            SDL_FRect bar = { k * barWidth, barBottomY - h, barWidth - 1, h };
            SDL_RenderFillRect(renderer, &bar);
        }

        // --- RENDER UI ---
        std::stringstream ss;
        std::string algoName, complexity, desc;

        switch(currentMode) {
            case BUBBLE_SORT:
                algoName = "Bubble Sort";
                complexity = "O(N^2) - Slow";
                desc = "Swaps adjacent elements repeatedly.";
                break;
            case SELECTION_SORT:
                algoName = "Selection Sort";
                complexity = "O(N^2) - Slow";
                desc = "Finds the smallest item and moves it.";
                break;
            case INSERTION_SORT:
                algoName = "Insertion Sort";
                complexity = "O(N^2) - OK for small lists";
                desc = "Builds sorted array one item at a time.";
                break;
            case QUICK_SORT:
                algoName = "Quick Sort";
                complexity = "O(N log N) - Fast";
                desc = "Divides list around a pivot point.";
                break;
            case MERGE_SORT:
                algoName = "Merge Sort";
                complexity = "O(N log N) - Stable";
                desc = "Divides list in half, sorts, and merges.";
                break;
        }

        if(isShuffling) {
            ss << "STATUS: Shuffling...";
        } else {
            // Use std::fixed and std::setprecision for readable decimals
            ss.precision(3);
            ss << std::fixed;

            ss << "ALGORITHM:  " << algoName << "\n"
               << "COMPLEXITY: " << complexity << "\n"
               << "HOW IT WORKS: " << desc << "\n\n"
               << "Comparisons:  " << comparisons << "\n"
               << "Swaps:        " << swaps << "\n"
               << "Real CPU Time:" << preciseTimeMs << "ms\n"
               << "Delay Added:  " << currentDelay << "ms";
        }

        RenderUI(renderer, ss.str());

        SDL_RenderPresent(renderer);
    }

    if (stream) SDL_DestroyAudioStream(stream);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}