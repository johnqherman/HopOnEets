// fna3d_null.c - a null FNA3D backend for the HEADLESS re-sim verifier (docs/headless-resim.md, Option A).
//
// The authoritative re-sim only needs the deterministic SIM to run - never a rendered frame. This library
// shadows the exact 29 FNA3D_* entry points Eets imports (enumerated from include/eets_addr_win.h) with
// no-ops that allocate dummy opaque handles, so the game boots and ticks its fixed-timestep sim with NO GPU
// and NO window. LD_PRELOAD it AFTER the framework loader (so the loader's FNA3D_SwapBuffers interpose still
// fires the mod's Update, then chains here via RTLD_NEXT and returns immediately). SDL's built-in `dummy`
// video/audio drivers cover windowing/input/audio - this only nulls the GPU layer.
//
// Status: stubs match the PUBLIC FNA3D API (https://github.com/FNA-XNA/FNA3D, FNA3D.h). Unvalidated against
// the shipped binary - it is the GL-less upgrade over the xvfb path (tools/resim-runner.sh, the validated
// default). If the game derefs a handle/field we don't populate, widen the stub. Build: `make` -> libnullbackend.so.
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// All FNA3D handles are opaque to the game; hand back a unique non-null pointer it never dereferences.
static void* dummy_handle(void) { return malloc(1); }

#define FNA3DAPI __attribute__((visibility("default")))

// --- device lifecycle ---
FNA3DAPI uint32_t FNA3D_PrepareWindowAttributes(void) { return 0; }   // 0 = no SDL_WINDOW_OPENGL needed (we stub the device)
FNA3DAPI void  FNA3D_GetDrawableSize(void* window, int32_t* w, int32_t* h) { if (w) *w = 1280; if (h) *h = 720; }
FNA3DAPI void* FNA3D_CreateDevice(void* presentationParameters, uint8_t debugMode) { (void)presentationParameters; (void)debugMode; return dummy_handle(); }
FNA3DAPI void  FNA3D_DestroyDevice(void* device) { free(device); }
FNA3DAPI void  FNA3D_SwapBuffers(void* device, void* src, void* dst, void* overrideWindowHandle) { (void)device; (void)src; (void)dst; (void)overrideWindowHandle; }
FNA3DAPI void  FNA3D_ResetBackbuffer(void* device, void* presentationParameters) { (void)device; (void)presentationParameters; }

// --- frame / draw (all no-ops; nothing is rendered) ---
FNA3DAPI void FNA3D_Clear(void* device, uint32_t options, void* color, float depth, int32_t stencil) { (void)device; (void)options; (void)color; (void)depth; (void)stencil; }
FNA3DAPI void FNA3D_DrawPrimitives(void* device, int32_t primitiveType, int32_t vertexStart, int32_t primitiveCount) { (void)device; (void)primitiveType; (void)vertexStart; (void)primitiveCount; }
FNA3DAPI void FNA3D_SetViewport(void* device, void* viewport) { (void)device; (void)viewport; }
FNA3DAPI void FNA3D_SetScissorRect(void* device, void* scissor) { (void)device; (void)scissor; }
FNA3DAPI void FNA3D_SetBlendState(void* device, void* blendState) { (void)device; (void)blendState; }
FNA3DAPI void FNA3D_SetDepthStencilState(void* device, void* depthStencilState) { (void)device; (void)depthStencilState; }
FNA3DAPI void FNA3D_ApplyRasterizerState(void* device, void* rasterizerState) { (void)device; (void)rasterizerState; }
FNA3DAPI void FNA3D_VerifySampler(void* device, int32_t index, void* texture, void* sampler) { (void)device; (void)index; (void)texture; (void)sampler; }
FNA3DAPI void FNA3D_ApplyVertexBufferBindings(void* device, void* bindings, int32_t numBindings, uint8_t bindingsUpdated, int32_t baseVertex) { (void)device; (void)bindings; (void)numBindings; (void)bindingsUpdated; (void)baseVertex; }
FNA3DAPI void FNA3D_SetRenderTargets(void* device, void* renderTargets, int32_t numRenderTargets, void* depthStencilBuffer, int32_t depthFormat, uint8_t preserveTargetContents) { (void)device; (void)renderTargets; (void)numRenderTargets; (void)depthStencilBuffer; (void)depthFormat; (void)preserveTargetContents; }
FNA3DAPI void FNA3D_ResolveTarget(void* device, void* target) { (void)device; (void)target; }

// --- textures ---
FNA3DAPI void* FNA3D_CreateTexture2D(void* device, int32_t format, int32_t width, int32_t height, int32_t levelCount, uint8_t isRenderTarget) { (void)device; (void)format; (void)width; (void)height; (void)levelCount; (void)isRenderTarget; return dummy_handle(); }
FNA3DAPI void  FNA3D_SetTextureData2D(void* device, void* texture, int32_t x, int32_t y, int32_t w, int32_t h, int32_t level, void* data, int32_t dataLength) { (void)device; (void)texture; (void)x; (void)y; (void)w; (void)h; (void)level; (void)data; (void)dataLength; }
FNA3DAPI void  FNA3D_GetTextureData2D(void* device, void* texture, int32_t x, int32_t y, int32_t w, int32_t h, int32_t level, void* data, int32_t dataLength) { (void)device; (void)texture; (void)x; (void)y; (void)w; (void)h; (void)level; if (data && dataLength > 0) memset(data, 0, (size_t)dataLength); }
FNA3DAPI void  FNA3D_AddDisposeTexture(void* device, void* texture) { (void)device; free(texture); }

// --- buffers ---
FNA3DAPI void* FNA3D_GenVertexBuffer(void* device, uint8_t dynamic, int32_t usage, int32_t sizeInBytes) { (void)device; (void)dynamic; (void)usage; (void)sizeInBytes; return dummy_handle(); }
FNA3DAPI void  FNA3D_SetVertexBufferData(void* device, void* buffer, int32_t offsetInBytes, void* data, int32_t elementCount, int32_t elementSizeInBytes, int32_t vertexStride, int32_t options) { (void)device; (void)buffer; (void)offsetInBytes; (void)data; (void)elementCount; (void)elementSizeInBytes; (void)vertexStride; (void)options; }
FNA3DAPI void  FNA3D_AddDisposeVertexBuffer(void* device, void* buffer) { (void)device; free(buffer); }

// --- effects (shaders) ---
FNA3DAPI void FNA3D_CreateEffect(void* device, uint8_t* effectCode, uint32_t effectCodeLength, void** effect, void** effectData) { (void)device; (void)effectCode; (void)effectCodeLength; if (effect) *effect = dummy_handle(); if (effectData) *effectData = dummy_handle(); }
FNA3DAPI void FNA3D_ApplyEffect(void* device, void* effect, void* technique, const void* stateChanges) { (void)device; (void)effect; (void)technique; (void)stateChanges; }
FNA3DAPI void FNA3D_AddDisposeEffect(void* device, void* effect) { (void)device; free(effect); }

// --- image codec (used to load textures from disk; the sim never reads the pixels) ---
FNA3DAPI uint8_t* FNA3D_Image_Load(void* readFunc, void* skipFunc, void* eofFunc, void* context, int32_t* w, int32_t* h, int32_t* len, int32_t forceW, int32_t forceH, uint8_t zoom) {
	(void)readFunc; (void)skipFunc; (void)eofFunc; (void)context; (void)zoom;
	int32_t ww = forceW > 0 ? forceW : 1, hh = forceH > 0 ? forceH : 1;
	if (w) *w = ww; if (h) *h = hh; if (len) *len = ww * hh * 4;
	return (uint8_t*)calloc((size_t)(ww * hh * 4), 1);   // opaque-black RGBA; FNA3D_Image_Free releases it
}
FNA3DAPI void FNA3D_Image_Free(uint8_t* mem) { free(mem); }
FNA3DAPI void FNA3D_Image_SavePNG(const char* filename, int32_t w, int32_t h, uint8_t srcW, uint8_t srcH, uint8_t* data) { (void)filename; (void)w; (void)h; (void)srcW; (void)srcH; (void)data; }
