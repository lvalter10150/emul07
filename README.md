# Émulateur Canon X-07

> Sauvegarder un programme en 2026 sans entendre un bruit de cassette, est-ce vraiment sauvegarder un programme ?

## Présentation

Bienvenue dans le projet **Émulateur Canon X-07**.

Le **Canon X-07**, c’est une petite machine des années 1980 qui tient presque dans la main, mais qui cache un vrai ordinateur programmable en BASIC.

Un écran LCD de 20 colonnes par 4 lignes, un clavier intégré, une interface cassette, du son, des ports d’extension, un BASIC Microsoft/Canon intégré… bref, tout ce qu’il faut pour replonger dans l’époque où sauvegarder un programme faisait du bruit sur une cassette audio.

Ce projet a pour but de faire revivre cette machine sous forme d’émulateur.

L’objectif n’est pas seulement d’afficher un prompt BASIC, mais de retrouver petit à petit le comportement du vrai Canon X-07 :

- démarrage du BASIC Canon X-07 ;
- clavier et touches spéciales ;
- affichage LCD ;
- affichage TV X-720 expérimental dans une seconde fenêtre SDL ;
- sons système et `BEEP` ;
- sauvegarde et chargement cassette avec `CSAVE` et `CLOAD` ;
- auto-chargement de fichiers cassette au lancement de l’émulateur ;
- injection automatique de listings BASIC depuis un fichier texte ;
- manipulation et conversion de fichiers cassette avec `x07cas` ;
- comportement mémoire lors de l’arrêt et du redémarrage ;
- émulation du processeur compatible Z80 / NSC800 ;
- et, si possible, ce petit charme imprévisible des machines de l’époque.

Le projet est encore en développement, mais il avance avec un objectif clair :

**faire tourner le Canon X-07 comme si la machine était encore posée sur le bureau, prête à afficher son prompt `>`**.

---

## Pourquoi ce projet ?

Parce qu’un Canon X-07, ce n’est pas seulement une vieille machine.

C’est :

- un prompt BASIC qui attend une commande ;
- des programmes sauvegardés sur cassette ;
- des bips qui sentent bon les années 80 ;
- un écran minuscule mais attachant ;
- une architecture à comprendre, décortiquer et refaire vivre ;
- et surtout une excellente excuse pour écrire du C, lire de la documentation ancienne et passer beaucoup trop de temps sur un curseur qui clignote.

Bref :

**un projet inutile donc absolument indispensable.**

---

## Remerciements et sources utilisées

Ce projet n’est pas né de zéro.

Il existe grâce au travail de passionnés qui ont ouvert la voie avant moi. Sans leurs recherches, leurs sources et leur travail de préservation, cet émulateur n’aurait probablement jamais vu le jour.

Un immense merci à **J. BRIGAUD**, dont les sources originales autour de l’émulation du **Canon X-07** ont servi de base précieuse pour comprendre la machine, son organisation interne et ses composants spécifiques.

Un grand merci également à **Marat Fayzullin**, auteur du cœur **Z80 portable**, utilisé comme base pour l’émulation du processeur compatible Z80 / NSC800. Son travail fournit une fondation solide pour exécuter le code machine du Canon X-07.

Merci donc à :

- **J. BRIGAUD**, pour les sources originales de l’émulateur Canon X-07 ;
- **Marat Fayzullin**, pour son cœur d’émulation Z80 portable ;
- tous les passionnés qui documentent, sauvegardent, scannent, testent, démontent, réparent et font vivre ces anciennes machines.

Ce dépôt est une continuation de ce travail, dans un esprit de préservation, d’expérimentation et de plaisir rétro-informatique.

---

## Fonctionnalités actuelles

Les éléments suivants sont partiellement ou totalement pris en charge :

- démarrage du BASIC Canon X-07 ;
- exécution du BASIC ;
- affichage texte via SDL2 pour le LCD du Canon X-07 ;
- seconde fenêtre SDL expérimentale pour le module TV X-720 ;
- intégration de la ROM X-720 / ROM TV en `A000h-AFFFh` ;
- VRAM X-720 séparée en `8000h-97FFh` ;
- premiers modes de rendu X-720 : visualisation mémoire, rendu texte ASCII simple et affichage par plans ;
- gestion du clavier PC vers clavier Canon X-07 ;
- touches spéciales `ON/BREAK`, `OFF`, `RESET` ;
- sauvegarde temporaire de la RAM lors de l’extinction ;
- support des fichiers cassette `.cas` ;
- chargement de programmes avec `CLOAD` ;
- auto-`CLOAD` d’un fichier `.cas` passé en argument au lancement ;
- injection automatique d’un listing BASIC texte avec l’option `.X07` ;
- sauvegarde de programmes avec `CSAVE` ;
- utilitaire `x07cas` pour manipuler et convertir les fichiers cassette ;
- génération sonore pendant les opérations cassette ;
- émulation des bips système et de la commande `BEEP`.

---

## Installation

### Dépendances

Sous Debian, Ubuntu ou dérivés :

```bash
sudo apt update
sudo apt install build-essential libsdl2-dev git
```

Selon les versions du projet, les bibliothèques suivantes peuvent également être nécessaires :

```bash
sudo apt install libsdl2-mixer-dev
```

### Récupération du projet

```bash
git clone https://github.com/lvalter10150/emul07.git
cd emul07
```

Ou, si le projet est déjà présent localement :

```bash
cd /home/lvalter/dev/canonx07
```

---

## Compilation

La compilation se fait avec `make` :

```bash
make
```

Si besoin, nettoyer les anciens fichiers compilés :

```bash
make clean
make
```

Le binaire principal de l’émulateur est :

```bash
./x07
```

Selon la version du projet, l’utilitaire cassette peut aussi être généré :

```bash
./x07cas
```

---

## Utilisation

### Lancement simple

Lancer l’émulateur :

```bash
./x07
```

Au démarrage, l’émulateur affiche l’écran BASIC du Canon X-07.

Exemple attendu :

```text
Copyright (C) 1983 by
Microsoft & Canon
xxxx Bytes Free
>
```

### Auto-CLOAD d’un fichier cassette

L’émulateur peut recevoir un fichier cassette `.cas` en argument :

```bash
./x07 toto.cas
```

Dans ce mode, l’émulateur démarre normalement, puis injecte automatiquement la commande BASIC de chargement cassette correspondant au fichier fourni.

Cela permet de lancer rapidement un programme cassette sans retaper manuellement :

```basic
CLOAD "TOTO"
```

Une fois le chargement terminé, il suffit généralement de vérifier le programme avec :

```basic
LIST
```

puis de l’exécuter avec :

```basic
RUN
```

### Injection d’un listing BASIC texte

Il est aussi possible d’envoyer automatiquement un listing BASIC contenu dans un fichier texte avec l’option `.X07` :

```bash
./x07 .X07 fichier_texte_basic.txt
```

Le fichier texte est alors injecté au clavier dans l’émulateur, comme si les lignes BASIC étaient tapées à la main sur le Canon X-07.

Exemple de fichier `fichier_texte_basic.txt` :

```basic
10 PRINT "HELLO CANON X-07"
20 FOR I=1 TO 10
30 PRINT I
40 NEXT I
```

Après l’injection, le programme peut être listé puis lancé :

```basic
LIST
RUN
```

---

## Module TV X-720

Le projet commence à intégrer le module TV **Canon X-720**.

Cette partie est encore expérimentale, mais plusieurs éléments importants fonctionnent déjà :

- chargement de la ROM X-720 / ROM TV ;
- mappage de la ROM TV en `A000h-AFFFh` ;
- ajout d’une VRAM X-720 séparée en `8000h-97FFh` ;
- gestion des ports vidéo `90h-97h` ;
- prise en compte du bit de banking `+1K` ;
- séparation de l’affichage LCD X-07 et de l’affichage TV X-720 ;
- création d’une seconde fenêtre SDL dédiée à la TV ;
- premiers rendus de debug par plans mémoire ;
- rendu texte ASCII simple pour valider les écritures en VRAM.

L’architecture actuellement utilisée est la suivante :

```text
8000h-97FFh : VRAM X-720
A000h-AFFFh : ROM X-720 / ROM TV
B000h-FFFFh : ROM BASIC Canon X-07
90h-97h     : registres vidéo / contrôle X-720
```

### Fenêtres SDL

L’émulateur utilise maintenant deux sorties vidéo séparées :

```text
Fenêtre LCD X-07 : affichage original 20 colonnes x 4 lignes
Fenêtre TV X-720 : affichage expérimental du module TV
```

Le fichier `video.c` reste consacré à l’écran LCD du Canon X-07.

Le fichier `video_x720.c` est dédié à la fenêtre TV X-720. Il permet de visualiser plusieurs plans de la VRAM et de tester les premiers rendus texte / graphique.

### Tests X-720 validés

Le mode `SCREEN 1` initialise correctement le module TV. Le test suivant écrit directement dans la VRAM texte :

```basic
10 SCREEN 1
20 POKE &H8000,65
30 POKE &H8001,66
40 POKE &H8020,67
50 POKE &H8021,68
60 GOTO 60
```

Avec le banking `+1K`, les écritures logiques en `8000h` sont visibles dans le plan physique `8400h`. Le rendu texte de la fenêtre TV affiche alors :

```text
AB
CD
```

Un test avec une chaîne complète fonctionne également :

```basic
10 SCREEN 1
20 A=&H8000
30 S$="HELLO X720"
40 FOR I=1 TO LEN(S$)
50 POKE A+I-1,ASC(MID$(S$,I,1))
60 NEXT I
70 GOTO 70
```

Ce test valide le chemin suivant :

```text
BASIC POKE &H8000
        ↓
WrZ80()
        ↓
X720_PhysAddr()
        ↓
X720_VRAM[]
        ↓
video_x720.c
        ↓
fenêtre SDL TV X-720
```

### Rendu texte actuel

Pour le moment, le rendu texte X-720 est volontairement simple. Il ne cherche pas encore à reproduire parfaitement le contrôleur vidéo réel, mais il permet de vérifier que les caractères écrits en VRAM apparaissent bien dans la fenêtre TV.

Le mode texte validé repose actuellement sur :

```text
SCREEN 1
CTRL=23h
PLUS1K=1
adresse logique texte : 8000h
adresse physique vue : 8400h
largeur : 32 caractères par ligne
```

Formule utilisée pour le rendu texte :

```c
offset = 0x0400 + y * 32 + x;   /* plan physique 8400h */
caractere = X720_VRAM[offset];
```

### Premières observations graphiques

Le mode `SCREEN 3` est également en cours d’analyse. Les traces ont montré que certaines écritures graphiques passent par la routine ROM autour de `A72Eh`.

Des tests avec `PSET` ont permis de comprendre un premier découpage en blocs :

```text
X=0..7     -> même octet VRAM
X=8..15    -> octet suivant
X=16..23   -> octet suivant
X=24..31   -> octet suivant
```

Et verticalement :

```text
Y=0..11    -> même rangée de blocs
Y=12..23   -> rangée suivante
Y=24..35   -> rangée suivante
Y=36..47   -> rangée suivante
```

La formule provisoire observée pour certains tests graphiques est :

```c
addr = 0x9000 + (y / 12) * 0x20 + (x / 8);
```

Cette partie reste expérimentale : elle sert pour l’instant à comprendre le codage de la VRAM et le comportement du contrôleur vidéo.

### Touches de debug de la fenêtre TV

La fenêtre TV X-720 dispose de quelques touches de debug selon la version de `video_x720.c` utilisée :

| Touche | Fonction |
|---|---|
| `0` | Afficher le plan `8000h` |
| `1` | Afficher le plan `8400h` |
| `2` | Afficher le plan `9000h` |
| `3` | Afficher le plan `9400h` |
| `TAB` | Changer de plan |
| `V` | Basculer entre mode valeurs / actif / texte |
| `T` | Passer en rendu texte |

Ces touches sont principalement destinées au debug de la VRAM X-720. Elles pourront évoluer au fur et à mesure de l’émulation réelle du contrôleur vidéo.

---

## Commandes BASIC utiles

### Tester le BASIC

```basic
PRINT 1+1
```

ou :

```basic
? 1+1
```

### Créer un petit programme

```basic
10 PRINT "HELLO CANON X-07"
20 GOTO 10
RUN
```

### Arrêter un programme

Utiliser la touche `ON/BREAK` émulée.

---

## Gestion cassette

Le projet prend en charge les commandes cassette du Canon X-07.

### Sauvegarder un programme

Dans le BASIC de l’émulateur :

```basic
CSAVE "TEST"
```

L’émulateur crée alors un fichier cassette, par exemple :

```text
TEST.cas
```

ou selon l’implémentation actuelle :

```text
test.cas
```

### Charger un programme

```basic
CLOAD "TEST"
```

L’émulateur cherche alors le fichier :

```text
TEST.cas
```

ou :

```text
test.cas
```

Puis charge le programme dans la mémoire BASIC.

### Utilitaire `x07cas`

Le projet inclut également l’utilitaire **`x07cas`**.

Cet outil permet de travailler directement avec les fichiers cassette utilisés par l’émulateur, sans forcément passer par l’interface BASIC du Canon X-07. Il accompagne les commandes `CSAVE` et `CLOAD` pour préparer, convertir ou contrôler les fichiers liés à la cassette.

Il permet notamment de convertir un fichier audio WAV en fichier cassette `.cas`, de générer un WAV à partir d’un fichier `.cas`, ou encore d’extraire les bits détectés depuis un WAV pour faciliter le debug.

#### Syntaxe

```text
./x07cas --wav2cas [options] input.wav output.cas
./x07cas --cas2wav [options] input.cas output.wav
./x07cas --bits   [options] input.wav output_bits.txt
```

#### Conversion WAV vers CAS

Convertit un enregistrement audio cassette en fichier `.cas` utilisable par l’émulateur.

```bash
./x07cas --wav2cas input.wav output.cas
```

Option disponible :

```text
--threshold R       seuil haut, défaut 0.666666
```

Exemple avec un seuil personnalisé :

```bash
./x07cas --wav2cas --threshold 0.60 sauvegarde.wav programme.cas
```

#### Conversion CAS vers WAV

Génère un fichier audio WAV à partir d’un fichier cassette `.cas`.

```bash
./x07cas --cas2wav input.cas output.wav
```

Options disponibles :

```text
--name NOM          nom cassette X-07, 6 caractères max, défaut CAS
--rate HZ           fréquence WAV, défaut 44100
```

Exemple :

```bash
./x07cas --cas2wav --name TEST --rate 44100 programme.cas programme.wav
```

#### Extraction des bits depuis un WAV

Le mode `--bits` permet de lire un fichier WAV et d’écrire les bits détectés dans un fichier texte. C’est pratique pour analyser un enregistrement, contrôler le décodage ou comprendre pourquoi une cassette ne se charge pas correctement.

```bash
./x07cas --bits input.wav output_bits.txt
```

#### Option commune

```text
--quiet             moins de messages
```

Exemple :

```bash
./x07cas --wav2cas --quiet input.wav output.cas
```

L’objectif est de faciliter les échanges entre le monde moderne — fichiers sur disque, fichiers texte, fichiers audio — et le comportement cassette historique du Canon X-07.

---

## Touches clavier

Les touches du PC sont converties vers les touches du Canon X-07.

| Touche PC | Fonction Canon X-07 |
|---|---|
| `F11` | OFF |
| `F12` | ON/BREAK |
| `Ctrl + R` | RESET |
| `Ctrl + Q` | Quitter l’émulateur |
| `F1` à `F6` | Touches de fonction Canon |
| Flèches | Déplacement curseur / STICK selon le contexte |

Certaines touches peuvent évoluer selon l’avancement de l’émulation.

---

## Organisation du projet

Les principaux fichiers du projet sont :

| Fichier | Rôle |
|---|---|
| `x07.c` | boucle principale de l’émulateur |
| `x07cas` / `x07cas.c` | utilitaire de manipulation et conversion cassette |
| `Z80.c` | cœur d’émulation Z80 / NSC800 |
| `T6834.c` | émulation du contrôleur clavier / affichage / E/S |
| `video.c` | gestion de l’affichage SDL du LCD X-07 |
| `video_x720.c` | gestion expérimentale de la seconde fenêtre SDL TV X-720 |
| `sound.c` | gestion des sons et bips |
| `Debug.c` | traces et fonctions de debug |
| `proto.h` | prototypes des fonctions |
| `struct.h` | structures principales |

---

## État du projet

Ce projet est expérimental.

Les points encore en cours ou à améliorer peuvent inclure :

- précision du timing NSC800 ;
- synchronisation vidéo ;
- gestion complète du T6834 ;
- gestion plus fidèle du clavier ;
- amélioration du son cassette ;
- support complet des périphériques ;
- amélioration du rendu TV X-720 et émulation plus fidèle du contrôleur vidéo ;
- bascule automatique propre entre les modes X-720 `SCREEN 1` et `SCREEN 3` ;
- amélioration du chargement et de la sauvegarde cassette ;
- compatibilité avec davantage de programmes BASIC.

---

## Licence

Ce projet est publié sous licence **GNU GPL v3 ou ultérieure**.

Vous êtes libres de l’utiliser, de l’étudier, de le modifier et de le redistribuer, à condition que les versions redistribuées restent sous la même licence et que le code source reste disponible.

L’objectif du projet est aussi communautaire : si vous améliorez l’émulateur, corrigez un bug ou ajoutez une fonctionnalité, merci de proposer vos changements au dépôt original afin que tout le monde puisse en profiter.

---

## Auteur et contributions

Projet repris, adapté et poursuivi par **Laurent Valter**.

Sources originales Canon X-07 : **J. BRIGAUD**  
Cœur Z80 portable : **Marat Fayzullin**

Dépôt GitHub :

```text
https://github.com/lvalter10150/emul07
```
