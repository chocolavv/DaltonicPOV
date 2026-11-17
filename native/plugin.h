// Plugin interface: any logic that turns the captured screen into what the overlay shows.
// Plain C with D3D11 COM pointers, so a plugin built by any compiler fits.
#pragma once
#include <d3d11.h>

#define DP_PLUGIN_VERSION 1

extern "C" {
struct dp_screen {  // one monitor, given to open()
    ID3D11Device* device;
    ID3D11DeviceContext* context;
    int width, height;
    double rgb_to_xyz[9];  // rows; the monitor's linear RGB -> CIE 1931 XYZ, from its EDID
    double gamma;          // encoded = linear^(1/gamma)
};

struct dp_plugin {
    int version;               // DP_PLUGIN_VERSION
    const wchar_t* (*name)();  // menu label, in the user's language
    void* (*open)(const dp_screen* screen);  // one per monitor; null = cannot run here
    // Draws the whole screen into `out` (viewport already set) from `screen`, the captured
    // desktop (B8G8R8A8, encoded). Called on the monitor's own thread for every new frame.
    void (*frame)(void* self, ID3D11ShaderResourceView* screen, ID3D11RenderTargetView* out);
    void (*close)(void* self);
};

// Every DLL in the app's plugins folder exports this: its plugins, ending with null.
typedef const dp_plugin* const* (*dp_plugins_fn)(void);
#define DP_PLUGINS_EXPORT "dp_plugins"
}
