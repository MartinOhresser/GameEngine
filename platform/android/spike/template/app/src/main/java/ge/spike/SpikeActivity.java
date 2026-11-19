package ge.spike;

import org.libsdl.app.SDLActivity;

/** The only Java of the game: names the libraries that hold the program. */
public class SpikeActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        // SPIKE_LIBRARIES is replaced by build.sh: "main" when SDL is linked into it,
        // "SDL3", "main" when SDL is a library of its own.
        return new String[] { SPIKE_LIBRARIES };
    }
}
