#include "ReducedMotion.h"

#include <QtGlobal>

#if defined(Q_OS_MACOS)
#include <CoreFoundation/CoreFoundation.h>
#elif defined(Q_OS_WIN)
#include <windows.h>
#endif

namespace Studio {

bool prefersReducedMotion() {
    if (qEnvironmentVariableIntValue("ANYDEVKIT_STUDIO_REDUCED_MOTION") != 0) return true;
#if defined(Q_OS_MACOS)
    Boolean valid = false;
    const auto value = CFPreferencesGetAppBooleanValue(CFSTR("reduceMotion"), CFSTR("com.apple.universalaccess"), &valid);
    return valid && value;
#elif defined(Q_OS_WIN)
    BOOL animations = TRUE;
    return SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0) && !animations;
#else
    return false;
#endif
}

}
