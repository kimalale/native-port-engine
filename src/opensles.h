#ifndef OPENSLES_H
#define OPENSLES_H

#include "fake_jni.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*AudioMix_fn)(JNIEnv *env, jclass clazz, jobject buf, jint frames);

/* Initialize the unified SDL2 audio engine (supports both OpenSL ES and JNI AudioMix). */
void opensles_init(void);
void opensles_shutdown(void);

/* Optional JNI AudioMix callback registration (used by Polarbit Fuse / JNI games). */
void audio_set_jni_mixer(AudioMix_fn mix_func);
void audio_start(void);
void audio_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENSLES_H */
