// Built with -DTDECK_ENABLE_MUSIC=0: the backend must behave exactly like the old midi_none.c.
#include "../../src/midi_tdeck.cpp"
#include <cstdio>
int main() {
    int32_t a[8] = {5,5,5,5,5,5,5,5}; bool init = midi_init(); midi_send(0x453C90); TDeck_Music_Render(a, 8);
    bool same = true; for (int v : a) if (v != 5) same = false;
    printf("music OFF: midi_init=%d (want 0), render adds nothing=%d (want 1)\n", init, same); return (init || !same);
}
