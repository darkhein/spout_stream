// Recepteur Spout de test : se connecte a un sender pendant N secondes puis affiche
//   connected=<0|1> size=<L>x<H> frames=<n> center_bgra=<b>,<g>,<r>,<a>
// Usage : spout_probe <nom_sender> [secondes]
#include <windows.h>
#include "SpoutDX.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

int main(int argc, char** argv)
{
    if (argc < 2) {
        printf("Usage : spout_probe <nom_sender> [secondes]\n");
        return 2;
    }
    const double seconds = argc > 2 ? atof(argv[2]) : 2.0;

    spoutDX receiver;
    if (!receiver.OpenDirectX11()) {
        printf("Erreur : DirectX 11 indisponible\n");
        return 2;
    }
    receiver.SetReceiverName(argv[1]);

    std::vector<unsigned char> pixels;
    int frames = 0;
    const ULONGLONG end = GetTickCount64() + (ULONGLONG)(seconds * 1000.0);
    while (GetTickCount64() < end) {
        // Premier appel (tampon vide) : connexion et lecture de la taille du sender
        if (receiver.ReceiveImage(pixels.empty() ? nullptr : pixels.data(),
                                  receiver.GetSenderWidth(), receiver.GetSenderHeight())) {
            if (receiver.IsUpdated())
                pixels.assign((size_t)receiver.GetSenderWidth() * receiver.GetSenderHeight() * 4, 0);
            else if (receiver.IsFrameNew())
                ++frames;
        }
        Sleep(1);
    }

    const unsigned w = receiver.GetSenderWidth(), h = receiver.GetSenderHeight();
    int b = -1, g = -1, r = -1, a = -1;
    if (!pixels.empty() && w && h) {
        const unsigned char* c = &pixels[((size_t)(h / 2) * w + w / 2) * 4];
        b = c[0]; g = c[1]; r = c[2]; a = c[3];
    }
    printf("connected=%d size=%ux%u frames=%d center_bgra=%d,%d,%d,%d\n",
        receiver.IsConnected() ? 1 : 0, w, h, frames, b, g, r, a);
    receiver.ReleaseReceiver();
    return 0;
}
