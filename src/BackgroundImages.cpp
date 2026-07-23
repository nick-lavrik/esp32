// BackgroundImages.cpp
#include "BackgroundImages.h"

#include "../assets/space-01.h"
#include "../assets/space-02.h"
#include "../assets/space-03.h"

const uint16_t* const backgroundImages[BACKGROUND_IMAGES_COUNT] = {
    backgroundSpace01,
    backgroundSpace02,
    backgroundSpace03,
};

size_t nextBackgroundIndex(size_t currentIndex) {
    return (currentIndex + 1) % BACKGROUND_IMAGES_COUNT;
}
