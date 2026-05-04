#include <X11/Xlib.h>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <iostream>
#include <map>
#include <memory>
#include <condition_variable>

#include <Average.h>
#include <Player.h>
#include <Map.h>
#include <WindowManager.h>
#include <Raycaster.h>
#include <UDPReceiver.h>
#include <UDPSender.h>
#include <DoubleBuffer.h>
#include <util.h>

struct ProgramArguments
{
    int screenWidth;
    int screenHeight;
    std::string ipsPath;
};

ProgramArguments parseArgs(int argc, char *argv[])
{
    if (argc != 4)
    {
        std::cerr << "Usage: " << argv[0] << " <screenWidth> <screenHeight> <ipsPath>" << std::endl;
        std::cerr << "  screenWidth: The width of the screen." << std::endl;
        std::cerr << "  screenHeight: The height of the screen." << std::endl;
        std::cerr << "  ipsPath: The path to the file containing the IP addresses and ports of the players." << std::endl;
        std::cerr << "Example: " << argv[0] << " 1920 1080 ips.txt" << std::endl;
        exit(1);
    }

    ProgramArguments args;
    args.screenWidth = std::stoi(argv[1]);
    args.screenHeight = std::stoi(argv[2]);
    args.ipsPath = argc == 4 ? argv[3] : "";
    return args;
}

int main(int argc, char *argv[])
{
    XInitThreads();
    ProgramArguments args = parseArgs(argc, argv);
    const int screenWidth = args.screenWidth;
    const int screenHeight = args.screenHeight;

    std::vector<std::unique_ptr<UDPSender>> udpSenders;
    NetworkData data = parseIPs(args.ipsPath);
    UDPReceiver udpReceiver(data.listeningPort);
    for (auto ipPort : data.ipPorts)
        udpSenders.push_back(std::unique_ptr<UDPSender>(new UDPSender(ipPort.first, ipPort.second)));
    size_t nbPlayers = udpSenders.size();

    // Indexes used to identify other players
    int nextPlayerIndex = 0;
    std::map<std::string, int> playersIndexes; // Maps IP addresses and ports to player indexes

    Map map = Map::generateMap(nbPlayers);
    Player player({22, 11.5}, {-1, 0}, {0, 0.66}, 5, 3, map);
    DoubleBuffer doubleBuffer(screenWidth, screenHeight);
    WindowManager windowManager(doubleBuffer);
    Raycaster raycaster(player, doubleBuffer, map);

    std::chrono::time_point<std::chrono::system_clock> time = std::chrono::system_clock::now(), oldTime;

    Average fpsCounter(1.0);

    std::mutex displayMutex;           // Protégera l'accès au DoubleBuffer
    std::atomic<bool> isRunning(true); // Permettra d'arrêter le thread proprement
    std::mutex mapMutex; // Mutex pour protéger les modifications de la carte (positions des joueurs)
    std::mutex sendMutex;
    std::condition_variable cvSend;
    bool hasMoved = false;    
    double safePosX = player.posX(); // Copie thread-safe de la position
    double safePosY = player.posY();

    // Création du Thread dédié à l'interface graphique
    std::thread guiThread([&]() {
        while (isRunning)
        {
            // On verrouille le mutex UNIQUEMENT pendant la copie de l'image
            {
                std::lock_guard<std::mutex> lock(displayMutex);
                windowManager.updateDisplay();
            }
            
            // X11 traite les événements clavier (pas besoin du mutex pour ça)
            windowManager.updateInput();

            // On endort le thread ~16ms pour limiter les FPS de l'affichage à ~60
            // Cela évite que le thread ne consomme 100% d'un coeur CPU pour rien.
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    });

    // Création du Thread dédié à la réception réseau
    std::thread receiverThread([&]() {
        while (isRunning)
        {
            // Le thread va s'endormir ici. 
            // Il se réveille soit avec un paquet, soit après 100ms.
            UDPData data = udpReceiver.receive(); 
            
            if (data.valid)
            {
                // On verrouille la carte uniquement quand on la modifie
                std::lock_guard<std::mutex> lock(mapMutex);
                
                if (playersIndexes.find(data.sender) == playersIndexes.end())
                {
                    playersIndexes[data.sender] = nextPlayerIndex++;
                    nextPlayerIndex %= nbPlayers;
                }
                int index = playersIndexes[data.sender];
                map.movePlayer(index, data.position.x(), data.position.y());
            }
        }
    });

    std::thread senderThread([&]() {
        while (isRunning)
        {
            // unique_lock est OBLIGATOIRE avec les variables conditionnelles
            std::unique_lock<std::mutex> lock(sendMutex);
            
            // Le thread s'endort ici. Il ne sera réveillé que si (hasMoved == true) 
            // ou si le jeu se ferme (!isRunning)
            cvSend.wait(lock, [&]() { return hasMoved || !isRunning; });

            if (!isRunning) break; // Si on quitte le jeu, on sort de la boucle

            // On récupère les coordonnées de manière sécurisée
            double px = safePosX;
            double py = safePosY;
            
            // On réinitialise le flag et on DÉVERROUILLE avant d'utiliser le réseau
            hasMoved = false;
            lock.unlock(); 

            // Envoi réseau (maintenant hors du verrou pour ne pas bloquer le jeu)
            for (auto &udpSender : udpSenders) {
                udpSender->send(px, py);
            }
        }
    });

    auto previousTime = std::chrono::high_resolution_clock::now();

    // Forcer l'envoi de la position initiale
    // pour ne pas être "invisible" au lancement du jeu.
    {
        std::lock_guard<std::mutex> lock(sendMutex);
        hasMoved = true;
    }
    cvSend.notify_one();

    while (true)
    {
        raycaster.castFloorCeiling();
        raycaster.castWalls();
        raycaster.castSprites();

        doubleBuffer.swap();

        oldTime = time;
        time = std::chrono::system_clock::now();
        std::chrono::duration<double> elapsed = time - oldTime;
        double frameTime = elapsed.count();

        fpsCounter.update(1.0 / frameTime);
        std::cout << "\r" << std::to_string(int(fpsCounter.get())) << " FPS" << std::flush;

        unsigned int keys = windowManager.getKeysPressed();
        
        bool playerMovedThisFrame = false; // Flag local pour cette itération

        if (keys & WindowManager::KEY_UP) {
            player.move(frameTime);
            playerMovedThisFrame = true;
        }
        if (keys & WindowManager::KEY_DOWN) {
            player.move(-frameTime);
            playerMovedThisFrame = true;
        }
        if (keys & WindowManager::KEY_RIGHT) {
            player.turn(-frameTime);
            playerMovedThisFrame = true; // Tourner compte comme un mouvement (changement de vue)
        }
        if (keys & WindowManager::KEY_LEFT) {
            player.turn(frameTime);
            playerMovedThisFrame = true;
        }
        if (keys & WindowManager::KEY_ESC) {
            break; // Quitte la boucle principale
        }

        // Si le joueur a bougé ce tour-ci, on signale le thread d'envoi
        if (playerMovedThisFrame)
        {
            {
                // On verrouille brièvement pour mettre à jour la position partagée
                std::lock_guard<std::mutex> lock(sendMutex);
                safePosX = player.posX();
                safePosY = player.posY();
                hasMoved = true;
            }
            // On réveille le thread d'envoi (notify_one = réveille un seul thread)
            cvSend.notify_one(); 
        }

        raycaster.castFloorCeiling();
        raycaster.castWalls();

        // On protège la lecture des sprites
        // car le receiverThread pourrait modifier un joueur au même moment.
        {
            std::lock_guard<std::mutex> lock(mapMutex);
            raycaster.castSprites();
        }

        // On verrouille le Mutex avant d'échanger les buffers
        // pour être certain que le guiThread n'est pas en train de lire le back buffer.
        {
            std::lock_guard<std::mutex> lock(displayMutex);
            doubleBuffer.swap();
        }
    }

    isRunning = false;

    // le senderThread afin qu'il lise isRunning == false et s'arrête.
    cvSend.notify_all();
    
    // On attend que le thread GUI ait terminé proprement avant de quitter le programme
    if (guiThread.joinable()) {
        guiThread.join();
    }

    if(receiverThread.joinable()) {
        receiverThread.join();
    }

    if(senderThread.joinable()) {
        senderThread.join();
    }

    return 0;
}
