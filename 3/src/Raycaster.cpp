/**
 * The following implementation is mainly based on the tutorial by Lode Vandevenne: https://lodev.org/cgtutor/raycasting.html
 */

#include <cmath>
#include <algorithm>

#include <Raycaster.h>

Raycaster::Raycaster(Player &player, DoubleBuffer &doubleBuffer, Map &map) : player(player),
                                                                             doubleBuffer(doubleBuffer),
                                                                             map(map),
                                                                             screenWidth(doubleBuffer.getWidth()),
                                                                             screenHeight(doubleBuffer.getHeight()),
                                                                             floorTexture(map.getFloorTexture()),
                                                                             ceilingTexture(map.getCeilingTexture()),
                                                                             zBuffer(screenWidth),
                                                                             spriteOrder(map.getSprites().size()),
                                                                             spriteDistance(map.getSprites().size()),
                                                                             numSprites(map.getSprites().size())
{
}

void Raycaster::castFloorCeiling()
{
    // Vertical position of the camera.
    double posZ = 0.5 * screenHeight;

    // On parallélise la boucle sur les lignes (y)
    #pragma omp parallel for
    for (int y = screenHeight / 2; y < screenHeight; y++)
    {
        // pour que chaque thread ait sa propre copie et éviter les Data Races.
        Vector<double> rayDir0 = {player.dirX() - player.camX(), player.dirY() - player.camY()};
        Vector<double> rayDir1 = {player.dirX() + player.camX(), player.dirY() + player.camY()};

        // Current y position compared to the center of the screen (the horizon)
        int p = y - screenHeight / 2;

        // Horizontal distance from the camera to the floor for the current row.
        double rowDistance = posZ / p;

        // calculate the real world step vector we have to add for each x
        double floorStepX = rowDistance * (rayDir1.x() - rayDir0.x()) / screenWidth;
        double floorStepY = rowDistance * (rayDir1.y() - rayDir0.y()) / screenWidth;

        // real world coordinates of the leftmost column
        double floorXBasis = player.posX() + rowDistance * rayDir0.x();
        double floorYBasis = player.posY() + rowDistance * rayDir0.y();

        for (int x = 0; x < screenWidth; ++x)
        {
            double floorX = floorXBasis + x * floorStepX;
            double floorY = floorYBasis + x * floorStepY;

            int cellX = int(floorX);
            int cellY = int(floorY);

            int tx = int(floorTexture.getWidth() * (floorX - cellX)) & (floorTexture.getWidth() - 1);
            int ty = int(floorTexture.getHeight() * (floorY - cellY)) & (floorTexture.getHeight() - 1);

            unsigned int color;

            // floor
            color = floorTexture.get(tx, ty);
            color = (color >> 1) & 8355711; 
            doubleBuffer.drawPixel(x, y, color);

            // ceiling
            color = ceilingTexture.get(tx, ty);
            color = (color >> 1) & 8355711; 
            doubleBuffer.drawPixel(x, screenHeight - y - 1, color);
        }
    }
}

void Raycaster::castWalls()
{
    // On parallélise la boucle sur les colonnes (x)
    #pragma omp parallel for
    for (int x = 0; x < screenWidth; x++)
    {
        // calculate ray position and direction
        double cameraX = 2 * x / double(screenWidth) - 1; 
        Vector<double> ray = player.generateRay(cameraX);
        
        int mapX = int(player.posX());
        int mapY = int(player.posY());

        double sideDistX;
        double sideDistY;

        double deltaDistX = (ray.x() == 0) ? 1e30 : std::abs(1 / ray.x());
        double deltaDistY = (ray.y() == 0) ? 1e30 : std::abs(1 / ray.y());
        double perpWallDist;

        int stepX;
        int stepY;

        int hit = 0; 
        int side;    
        
        if (ray.x() < 0)
        {
            stepX = -1;
            sideDistX = (player.posX() - mapX) * deltaDistX;
        }
        else
        {
            stepX = 1;
            sideDistX = (mapX + 1.0 - player.posX()) * deltaDistX;
        }
        if (ray.y() < 0)
        {
            stepY = -1;
            sideDistY = (player.posY() - mapY) * deltaDistY;
        }
        else
        {
            stepY = 1;
            sideDistY = (mapY + 1.0 - player.posY()) * deltaDistY;
        }
        
        while (hit == 0)
        {
            if (sideDistX < sideDistY)
            {
                sideDistX += deltaDistX;
                mapX += stepX;
                side = 0;
            }
            else
            {
                sideDistY += deltaDistY;
                mapY += stepY;
                side = 1;
            }
            if (map.get(mapX, mapY) > 0)
                hit = 1;
        }
        
        if (side == 0)
            perpWallDist = (sideDistX - deltaDistX);
        else
            perpWallDist = (sideDistY - deltaDistY);

        int lineHeight = int(screenHeight / perpWallDist);

        int drawStart = -lineHeight / 2 + screenHeight / 2;
        if (drawStart < 0)
            drawStart = 0;
        int drawEnd = lineHeight / 2 + screenHeight / 2;
        if (drawEnd >= screenHeight)
            drawEnd = screenHeight - 1;

        Texture texture = map.getTexture(mapX, mapY);

        double wallX; 
        if (side == 0)
            wallX = player.posY() + perpWallDist * ray.y();
        else
            wallX = player.posX() + perpWallDist * ray.x();
        wallX -= floor(wallX);

        int texX = int(wallX * double(texture.getWidth()));
        if (side == 0 && ray.x() > 0)
            texX = texture.getWidth() - texX - 1;
        if (side == 1 && ray.y() < 0)
            texX = texture.getWidth() - texX - 1;

        doubleBuffer.drawVertLine(x, drawStart, drawEnd, lineHeight, texture, texX, side == 1);

        zBuffer[x] = perpWallDist; // Sécurisé car chaque thread a un 'x' unique
    }
}

void Raycaster::castSprites()
{
    std::vector<Sprite> sprites = map.getSprites();

    int screenWidth = doubleBuffer.getWidth();
    int screenHeight = doubleBuffer.getHeight();

    for (int i = 0; i < numSprites; i++)
    {
        spriteOrder[i] = i;
        Sprite sprite = sprites[i];
        spriteDistance[i] = pow(player.posX() - sprite.posX(), 2) + pow(player.posY() - sprite.posY(), 2); 
    }

    sortSprites();

    // sur le dessin des sprites qui se chevauchent.
    for (int i = 0; i < numSprites; i++)
    {
        Sprite sprite = sprites[spriteOrder[i]];

        double spriteX = sprite.posX() - player.posX();
        double spriteY = sprite.posY() - player.posY();

        double invDet = 1.0 / (player.camX() * player.dirY() - player.dirX() * player.camY()); 

        double transformX = invDet * (player.dirY() * spriteX - player.dirX() * spriteY);
        double transformY = invDet * (-player.camY() * spriteX + player.camX() * spriteY); 

        int spriteScreenX = int((screenWidth / 2) * (1 + transformX / transformY));

        int spriteHeight = abs(int(screenHeight / (transformY))); 
        int drawStartY = -spriteHeight / 2 + screenHeight / 2;
        if (drawStartY < 0)
            drawStartY = 0;
        int drawEndY = spriteHeight / 2 + screenHeight / 2;
        if (drawEndY >= screenHeight)
            drawEndY = screenHeight - 1;

        int spriteWidth = abs(int(screenHeight / (transformY)));
        int drawStartX = -spriteWidth / 2 + spriteScreenX;
        if (drawStartX < 0)
            drawStartX = 0;
        int drawEndX = spriteWidth / 2 + spriteScreenX;
        if (drawEndX >= screenWidth)
            drawEndX = screenWidth - 1;

        // On parallélise le dessin des colonnes verticales du sprite
        #pragma omp parallel for
        for (int stripe = drawStartX; stripe < drawEndX; stripe++)
        {
            int texX = int(256 * (stripe - (-spriteWidth / 2 + spriteScreenX)) * sprite.getWidth() / spriteWidth) / 256;
            
            if (transformY > 0 && stripe > 0 && stripe < screenWidth && transformY < zBuffer[stripe])
                for (int y = drawStartY; y < drawEndY; y++) 
                {
                    int d = (y) * 256 - screenHeight * 128 + spriteHeight * 128; 
                    int texY = ((d * sprite.getHeight()) / spriteHeight) / 256;
                    unsigned int color = sprite.get(texX, texY); 
                    if ((color & 0x00FFFFFF) != 0)
                        doubleBuffer.drawPixel(stripe, y, color); 
                }
        }
    }
}

void Raycaster::sortSprites()
{
    std::vector<std::pair<double, int>> sprites(numSprites);
    for (int i = 0; i < numSprites; i++)
    {
        sprites[i].first = spriteDistance[i];
        sprites[i].second = spriteOrder[i];
    }
    std::sort(sprites.begin(), sprites.end());
    // restore in reverse order to go from farthest to nearest
    for (int i = 0; i < numSprites; i++)
    {
        spriteDistance[i] = sprites[numSprites - i - 1].first;
        spriteOrder[i] = sprites[numSprites - i - 1].second;
    }
}