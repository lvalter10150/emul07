# Émulateur Canon X-07

> Sauvegarder un programme en 2026 sans entendre un bruit de cassette, est-ce vraiment sauvegarder un programme ?

## Présentation

Bienvenue dans le projet **Émulateur Canon X-07**.

Le **Canon X-07**, c’est une petite machine des années 1980 qui tient presque dans la main, mais qui cache un vrai ordinateur programmable en BASIC.

Un écran LCD de 20 colonnes par 4 lignes, un clavier intégré, une interface cassette, du son, des ports d’extension, un BASIC Microsoft/Canon en ROM… bref, tout ce qu’il faut pour replonger dans l’époque où sauvegarder un programme faisait du bruit sur une cassette audio.

Ce projet a pour but de faire revivre cette machine sous forme d’émulateur.

L’objectif n’est pas seulement d’afficher un prompt BASIC, mais de retrouver petit à petit le comportement du vrai Canon X-07 :

- démarrage de la ROM ;
- clavier et touches spéciales ;
- affichage LCD ;
- sons système et `BEEP` ;
- sauvegarde et chargement cassette avec `CSAVE` et `CLOAD` ;
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

Un grand merci également à **Marat Fayzullin**, auteur du cœur **Z80 portable**, utilisé comme base pour l’émulation du processeur compatible Z80 / NSC800. Son travail fournit une fondation solide pour exécuter le code machine de la ROM du Canon X-07.

Merci donc à :

- **J. BRIGAUD**, pour les sources originales de l’émulateur Canon X-07 ;
- **Marat Fayzullin**, pour son cœur d’émulation Z80 portable ;
- tous les passionnés qui documentent, sauvegardent, scannent, testent, démontent, réparent et font vivre ces anciennes machines.

Ce dépôt est une continuation de ce travail, dans un esprit de préservation, d’expérimentation et de plaisir rétro-informatique.

---

## Fonctionnalités actuelles

Les éléments suivants sont partiellement ou totalement pris en charge :

- démarrage de la ROM Canon X-07 ;
- exécution du BASIC ;
- affichage texte via SDL2 ;
- gestion du clavier PC vers clavier Canon X-07 ;
- touches spéciales `ON/BREAK`, `OFF`, `RESET` ;
- sauvegarde temporaire de la RAM lors de l’extinction ;
- support des fichiers cassette `.cas` ;
- chargement de programmes avec `CLOAD` ;
- sauvegarde de programmes avec `CSAVE` ;
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

Le binaire généré dépend du `Makefile`, mais il peut par exemple s’appeler :

```bash
./x07
```

ou :

```bash
./canonx07
```

---

## Utilisation

Lancer l’émulateur :

```bash
./x07
```

ou selon le nom du binaire :

```bash
./canonx07
```

Au démarrage, l’émulateur lance la ROM du Canon X-07 et affiche l’écran BASIC.

Exemple attendu :

```text
Copyright (C) 1983 by
Microsoft & Canon
xxxx Bytes Free
>
```

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

### Vérifier le programme chargé

```basic
LIST
```

Puis lancer :

```basic
RUN
```

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
| `Z80.c` | cœur d’émulation Z80 / NSC800 |
| `T6834.c` | émulation du contrôleur clavier / affichage / E/S |
| `video.c` | gestion de l’affichage SDL |
| `sound.c` | gestion des sons et bips |
| `Debug.c` | traces et fonctions de debug |
| `proto.h` | prototypes des fonctions |
| `struct.h` | structures principales |

---

## ROM Canon X-07

L’émulateur nécessite une image ROM du Canon X-07 pour fonctionner.

Pour des raisons de droits, la ROM originale n’est pas fournie dans ce dépôt.

Placez votre fichier ROM dans le répertoire attendu par le programme, selon la configuration du projet.

Exemple :

```text
roms/x07.rom
```

ou adaptez le chemin dans le code source si nécessaire.

---

## Fichiers à ne pas publier

Il est recommandé de ne pas publier dans GitHub :

- les fichiers ROM ;
- les fichiers cassette personnels ;
- les fichiers WAV générés ;
- les binaires compilés ;
- les fichiers temporaires ;
- les fichiers de debug.

Exemple de `.gitignore` :

```gitignore
*.o
*.a
*.so
*.exe
*.out
*.bin
*.rom
*.cas
*.wav
*.log

build/
Debug/
Release/
.vscode/
roms/
```

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
- amélioration du chargement et de la sauvegarde cassette ;
- compatibilité avec davantage de programmes BASIC.

---

## Licence

Licence à définir.

Attention : le code source de l’émulateur peut être publié sous une licence libre, mais les ROMs originales Canon ne doivent pas être redistribuées sans autorisation.

---

## Auteur et contributions

Projet repris, adapté et poursuivi par **Laurent Valter**.

Sources originales Canon X-07 : **J. BRIGAUD**  
Cœur Z80 portable : **Marat Fayzullin**

Dépôt GitHub :

```text
https://github.com/lvalter10150/emul07
```
