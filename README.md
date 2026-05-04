***

```markdown
# 🎮 Projet INFO9012-1 : Parallélisation d'un Moteur de Raycasting

**Auteur :** Franck Duval HEUBA - S227629
**Cours :** INFO9012-1 Parallel Programming (Université de Liège)

## 📝 Description du Projet
Ce projet consiste en la transformation d'un moteur de jeu 3D (Raycasting) purement séquentiel en une application hautement concurrente et parallèle. L'objectif mathématique du raycasting (DDA, calcul des rayons) est resté intact. Tout le travail s'est concentré sur l'optimisation de l'architecture logicielle afin d'exploiter pleinement les processeurs multicœurs modernes, tout en garantissant l'absence de *data races* et de *deadlocks*.

---

## ⚙️ Compilation et Exécution

Pour compiler le projet avec toutes les optimisations requises (OpenMP, Threads POSIX, X11 et l'optimisation de niveau 3 du compilateur), exécutez la commande suivante à la racine du projet :

```bash
g++ -O3 -fopenmp -pthread *.cpp -o raycaster -lX11
```

**Exécution :**
```bash
./raycaster 800 600 [ips.txt]
```
*(Où 800 et 600 représentent la résolution de la fenêtre. Le fichier `ips.txt` est optionnel pour le mode multijoueur).*

---

## 🏗️ Architecture et Optimisations (Les 4 Missions)

Afin de rendre ce programme efficace, j'ai divisé mon travail en quatre grandes étapes incrémentales.

### Étape 1 : Accélération du Rendu 3D avec OpenMP
Le goulet d'étranglement principal du programme séquentiel était le calcul mathématique de l'affichage (sol, plafond, murs, sprites). J'ai utilisé l'API **OpenMP** pour distribuer ces calculs sur plusieurs cœurs.

* **Sols, Plafonds et Murs (`castFloorCeiling` & `castWalls`) :** J'ai parallélisé les boucles principales (`#pragma omp parallel for`) qui parcourent l'écran ligne par ligne ou colonne par colonne. 
  * *Défi technique :* Pour éviter les *data races*, je me suis assuré que chaque thread possédait ses propres copies des variables de calcul (ex: déplacement des déclarations `rayDir0` et `rayDir1` à l'intérieur de la boucle).
* **Sprites (`castSprites`) :** Je n'ai **pas** parallélisé la boucle externe (qui itère sur chaque sprite) car si deux sprites se chevauchent, deux threads essaieraient d'écrire sur le même pixel au même moment, brisant l'ordre d'affichage (Z-indexing). J'ai plutôt parallélisé la boucle interne (le dessin des colonnes verticales de chaque sprite).
* **Tri des Sprites (`sortSprites`) :** J'ai fait le choix de laisser le tri séquentiel. Le ratio coût/bénéfice (*overhead* de création des threads vs temps de tri de quelques éléments) aurait ralenti le programme (Loi d'Amdahl).

### Étape 2 : Multithreading de l'Interface Graphique (GUI)
Pour éviter que la gestion de la fenêtre ne ralentisse la logique du jeu, j'ai extrait l'affichage X11 et la capture du clavier dans un thread dédié (`guiThread`).

* **Sécurisation de X11 :** J'ai ajouté l'appel `XInitThreads()` au tout début du `main` pour rendre la librairie X11 compatible avec le multithreading.
* **Synchronisation de l'image :** Le Raycaster écrit dans un *DoubleBuffer*. Pour éviter le déchirement de l'image (*tearing*), j'ai protégé la fonction `doubleBuffer.swap()` et l'appel `windowManager.updateDisplay()` avec un `std::mutex` (`displayMutex`). J'ai utilisé des `std::lock_guard` couplés à des accolades `{ }` pour minimiser la taille de la section critique.
* **Sécurisation des Inputs :** La variable stockant l'état du clavier (`keysPressed`) est lue par le jeu et écrite par l'interface. Je l'ai transformée en `std::atomic<unsigned int>` pour garantir une communication lock-free et thread-safe.

### Étape 3 : Réception Réseau et Suppression de l'Attente Active
Le code d'origine vérifiait le réseau en boucle avec l'option `MSG_DONTWAIT`, causant une attente active (*busy-waiting*) qui saturait un cœur CPU à 100%.

* **Thread Bloquant :** J'ai créé un `receiverThread`. J'ai retiré le flag `MSG_DONTWAIT` pour rendre le socket bloquant. Grâce à l'option `SO_RCVTIMEO` pré-configurée, le thread s'endort et se réveille automatiquement si un paquet arrive ou au bout de 100ms (ce qui permet de vérifier proprement la condition de sortie du programme).
* **Protection de la Carte :** La réception réseau modifiant la position des autres joueurs sur la carte, et le moteur de rendu lisant cette même carte, j'ai introduit un `std::mutex` (`mapMutex`) pour protéger la consistance des données de la map.

### Étape 4 : Envoi Réseau à la Demande (Variables Conditionnelles)
Le comportement original inondait le réseau en envoyant la position du joueur à chaque frame, même s'il ne bougeait pas.

* **Variable Conditionnelle :** J'ai créé un `senderThread` qui dort indéfiniment en utilisant un `std::unique_lock` et la fonction `cvSend.wait()`.
* **Réveil intelligent :** Le thread de logique principale ne fait un `cvSend.notify_one()` que si le joueur a réellement appuyé sur une touche de mouvement (ou de rotation) durant la frame courante.
* **Correction du "Joueur Fantôme" :** J'ai forcé un signal d'envoi réseau juste avant d'entrer dans la boucle de jeu principale pour m'assurer que la position d'apparition initiale soit transmise aux autres joueurs, même sans interaction clavier.

---

## 📊 Conclusion
L'application est passée d'un processus monolithique bloquant à une architecture élégante utilisant :
1. Un thread principal (Logique de jeu & Orchestration).
2. Un pool de threads OpenMP (Calculs mathématiques lourds).
3. Un thread GUI (Affichage et événements).
4. Un thread de réception (Endormi 99% du temps, réveillé par les I/O).
5. Un thread d'envoi (Endormi, réveillé par condition variable).

Le moteur est désormais hautement performant, fluide, et a une empreinte CPU extrêmement réduite en mode veille.
```