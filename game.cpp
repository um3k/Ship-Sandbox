#include "game.h"

#include <map>
#include "util.h"


const int directions[8][2] = {
{ 1,  0},
{ 1, -1},
{ 0, -1},
{-1, -1},
{-1,  0},
{-1,  1},
{ 0,  1},
{ 1,  1}
};

vec2 game::screen2world(vec2 pos)
{
    float height = zoomsize * 2.0;
    float width = (float)canvaswidth / canvasheight * height;
    return vec2((pos.x / canvaswidth - 0.5) * width + camx,
                (pos.y / canvasheight - 0.5) * -height + camy);
}

void game::loadShip(std::string filename)
{
    // RGB channels contain separate information:
    // R: Strength (higher = more)
    // G: Empty or not (white background has high G so is ignored, all materials have low G)
    // B: Hull or not (blue has high G, is not hull; black has high G, is hull)
    // black: weak hull; blue: weak internal; red: strong hull; magenta: strong internal
    // Can vary shades of red for varying strengths and colours

    lastFilename = filename;

    int nodecount = 0, springcount = 0;

    std::map<vec3f, material*> colourdict;
    for (unsigned int i = 0; i < materials.size(); i++)
        colourdict[materials[i]->colour] = materials[i];

    wxImage shipimage(filename, wxBITMAP_TYPE_PNG);
    phys::ship *shp = new phys::ship(wld);

    std::map<int,  std::map <int, phys::point*> > points;

    for (int x = 0; x < shipimage.GetWidth(); x++)
    {
        for (int y = 0; y < shipimage.GetHeight(); y++)
        {
            vec3f colour(shipimage.GetRed  (x, shipimage.GetHeight() - y - 1) / 255.f,
                         shipimage.GetGreen(x, shipimage.GetHeight() - y - 1) / 255.f,
                         shipimage.GetBlue (x, shipimage.GetHeight() - y - 1) / 255.f);
            if (colourdict.find(colour) != colourdict.end())
            {
                material *mtl = colourdict[colour];
                points[x][y] = new phys::point(wld, vec2(x - shipimage.GetWidth()/2, y), mtl, mtl->isHull? 0 : 1);  // no buoyancy if it's a hull section
                shp->points.insert(points[x][y]);
                nodecount++;
            }
            else
            {
                points[x][y] = 0;
            }
        }
    }

    // Points have been generated, so fill in all the beams between them.
    // If beam joins two hull nodes, it is a hull beam.
    // If a non-hull node has empty space on one of its four sides, it is automatically leaking.

    for (int x = 0; x < shipimage.GetWidth(); x++)
    {
        for (int y = 0; y < shipimage.GetHeight(); y++)
        {
            phys::point *a = points[x][y];
            if (!a)
                continue;
            // First four directions out of 8: from 0 deg (+x) through to 135 deg (-x +y) - this covers each pair of points in each direction
            for (int i = 0; i < 4; i++)
            {
                phys::point *b = points[x + directions[i][0]][y + directions[i][1]];                        // adjacent point in direction (i)
                phys::point *c = points[x + directions[(i + 1) % 8][0]][y + directions[(i + 1) % 8][1]];    // adjacent point in next CW direction (for constructing triangles)
                if (b)
                {
                    bool pointIsHull = a->mtl->isHull;
                    bool isHull = pointIsHull && b->mtl->isHull;
                    material *mtl = b->mtl->isHull? a->mtl : b->mtl;    // the spring is hull iff both nodes are hull; if so we use the hull material.
                    shp->springs.insert(new phys::spring(wld, a, b, mtl, -1));
                    if (!isHull)
                    {
                        shp->adjacentnodes[a].insert(b);
                        shp->adjacentnodes[b].insert(a);
                    }
                    if (!(pointIsHull || (points[x+1][y] && points[x][y+1] && points[x-1][y] && points[x][y-1])))   // check for gaps next to non-hull areas:
                    {
                        a->isLeaking = true;
                    }
                    if (c)
                        shp->triangles.insert(new phys::ship::triangle(shp, a, b, c));
                    springcount++;
                }
            }
        }
    }
    std::cout << "Loaded ship \"" << filename << "\": " << nodecount << " points, " << springcount << " springs.\n";
}

void game::loadDepth(std::string filename)
{
    wxImage depthimage(filename, wxBITMAP_TYPE_PNG);
    oceandepthbuffer = new float[2048];
    for (unsigned i = 0; i < 2048; i++)
    {
        float xpos = i / 16.f;
        oceandepthbuffer[i] = depthimage.GetRed(floorf(xpos), 0) * (floorf(xpos) - xpos) + depthimage.GetRed(ceilf(xpos), 0) * (1 - (floorf(xpos) - xpos))
                            ;//+ depthimage.GetGreen(i % 256, 0) * 0.0625f;
        oceandepthbuffer[i] = oceandepthbuffer[i] * 1.f - 180.f;
    }
}


void game::assertSettings()
{
    wld->buoyancy = buoyancy;
    wld->strength = strength;
    wld->waterpressure = waterpressure;
    wld->waveheight = waveheight;
    wld->seadepth = seadepth;
    wld->showstress = showstress;
    wld->quickwaterfix = quickwaterfix;
    wld->oceandepthbuffer = oceandepthbuffer;
    wld->xraymode = xraymode;
}

void game::update()
{
    if (mouse.ldown)
    {
        if (tool == TOOL_SMASH)
            wld->destroyAt(screen2world(vec2(mouse.x, mouse.y)));
        else if (tool == TOOL_GRAB)
            wld->drawTo(screen2world(vec2(mouse.x, mouse.y)));
    }
    if (running)
        wld->update(0.02);
}

void game::render()
{
    float halfheight = zoomsize;
    float halfwidth = (float)canvaswidth / canvasheight * halfheight;
    wld->render(camx - halfwidth, camx + halfwidth, camy - halfheight, camx + halfheight);
}

game::game()
{
    Json::Value matroot = jsonParseFile("data/materials.json");
    for (unsigned int i = 0; i < matroot.size(); i++)
        materials.push_back(new material(matroot[i]));
    wld = new phys::world();
    loadDepth("data/depth.png");
    buoyancy = 4.0;
    strength = 0.01;
    waveheight = 1.0;
    waterpressure = 0.3;
    seadepth = 150;
    showstress = false;
    quickwaterfix = false;
    assertSettings();
}
