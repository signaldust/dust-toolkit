
#pragma once

namespace dust
{
    // Surface is a CPU-memory array of pixels
    //
    // NOTE: we might want to cache this in GPU eventually
    class Surface
    {
        ARGB    *pixels = 0;
        unsigned szX = 0, szY = 0, pitch = 0;
        bool    needUpdate = false;

    public:
        // create a new surface; defaults to zero area
        Surface(unsigned w = 0, unsigned h = 0, unsigned pAlign = 1)
        : pixels(0), szX(0), szY(0) { validate(w, h, pAlign); }

        // create a new surface, loading an image file (using stb_image)
        Surface(const std::vector<char> & fileContents);

        // allow move construction
        Surface(Surface && other)
        {
            std::swap(pixels, other.pixels);
            std::swap(szX, other.szX);
            std::swap(szY, other.szY);
            std::swap(pitch, other.pitch);
            std::swap(needUpdate, other.needUpdate);
        }

        // allow move assignment
        void operator=(Surface && other)
        {
            std::swap(pixels, other.pixels);
            std::swap(szX, other.szX);
            std::swap(szY, other.szY);
            std::swap(pitch, other.pitch);
            std::swap(needUpdate, other.needUpdate);
        }
        
        ~Surface() { if(pixels) { delete [] pixels; } }

        // return current dimensions
        unsigned getSizeX() { return szX; }
        unsigned getSizeY() { return szY; }
        // return pixel pitch
        unsigned getPitch() { return pitch; }

        // return pointer to pixels
        // this can be null if the surface has zero area
        //
        // NOTE: for GPU backing, should this invalidate?
        ARGB * getPixels() { return pixels; }

        // check surface size, resize if necessary
        // returns true if the surface was resized (= content lost)
        // returns false if the contents were preserved
        //
        // NOTE: for GPU backing, this should invalidate on resize
        bool validate(unsigned w, unsigned h, unsigned pAlign = 1)
        {
            bool defaultRet = needUpdate; needUpdate = false;
            if(szX == w && szY == h) return defaultRet;

            if(pixels) { delete [] pixels; }

            pitch = ((w + pAlign - 1) / pAlign) * pAlign;
            
            szX = w; szY = h;
            unsigned bufSize = pitch * szY;
            pixels = bufSize ? new ARGB[bufSize] : 0;
            return true;
        }

        // force next validate to return true even if no resize is done
        void invalidate()
        {
            needUpdate = true;
        }

        ///////////////
        // FILTER FX //
        ///////////////

        // blur src and place the result into this surface
        // will resize the surface to match the dimensions of src
        void blur(Surface & src, float radius);

        // in-place wrapper
        void blur(float radius) { blur(*this, radius); }

        // in-place fade edges (smoothstep with adjustable radius)
        // this can be used to cleanup blur
        void fadeEdges(float radius);

        // replace color with "diffuse light"
        // uses alpha channel as the height
        //
        // height should be a point-distance in pixels
        void emboss(float h = 1);

    };

};
