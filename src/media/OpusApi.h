#pragma once
#include <cstdint>
// Small declaration surface of the stable public libopus C ABI.
// https://opus-codec.org/docs/opus_api-1.6/
using opus_int16 = int16_t;
using opus_int32 = int32_t;
struct OpusEncoder;
struct OpusDecoder;
extern "C" {
OpusEncoder *opus_encoder_create(opus_int32 sampleRate, int channels, int application, int *error);
void opus_encoder_destroy(OpusEncoder *encoder);
int opus_encoder_ctl(OpusEncoder *encoder, int request, ...);
opus_int32 opus_encode(OpusEncoder *encoder, const opus_int16 *pcm, int frameSize, unsigned char *data, opus_int32 maxBytes);
const char *opus_strerror(int error);
const char *opus_get_version_string();
OpusDecoder *opus_decoder_create(opus_int32 sampleRate, int channels, int *error);
void opus_decoder_destroy(OpusDecoder *decoder);
int opus_decode(OpusDecoder *decoder, const unsigned char *data, opus_int32 length, opus_int16 *pcm, int frameSize, int fec);
}
inline constexpr int OPUS_OK = 0, OPUS_APPLICATION_VOIP = 2048;
inline constexpr int OPUS_SET_BITRATE_REQUEST = 4002, OPUS_SET_COMPLEXITY_REQUEST = 4010, OPUS_GET_LOOKAHEAD_REQUEST = 4027;
