package ge.player;

import org.libsdl.app.SDLActivity;

/** The only Java of an exported game: SDL is linked into libmain.so, so that is all to load. */
public class GameActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "main" };
    }
}
