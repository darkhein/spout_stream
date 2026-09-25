# spout_stream

[![CI](https://github.com/darkhein/spout_stream/actions/workflows/ci.yml/badge.svg)](https://github.com/darkhein/spout_stream/actions/workflows/ci.yml)

Diffuse un écran ou une fenêtre Windows en flux **Spout**. C'est l'équivalent de NDI Screen Capture, mais en local, via une texture GPU partagée.

## Principe (latence minimale)

```
Windows.Graphics.Capture ──(texture D3D11)──► CopySubresourceRegion ──► texture partagée Spout ──► récepteurs
```

- Tout reste sur le GPU : aucune copie vers la mémoire CPU, aucune compression.
- Chaque image est envoyée dès que Windows la livre (callback du frame pool, thread libre). Si plusieurs images sont en attente, seule la plus récente est gardée.
- Le frame pool n'a que 2 tampons, et la limite de cadence par défaut de Windows 11 (`MinUpdateInterval`) est levée.

## Téléchargement

La dernière version compilée se trouve dans les [Releases](https://github.com/darkhein/spout_stream/releases). Décompressez le zip où vous voulez : rien d'autre n'est à installer.

## Compilation

Prérequis : Visual Studio 2019/2022 avec la charge « Développement Desktop en C++ » (SDK Windows 10/11 inclus).

```bat
build.bat
```

Le SDK Spout2 est lu dans `third_party/Spout2`. S'il est absent, CMake télécharge la version figée, dont l'empreinte SHA-256 est vérifiée.

Pour compiler et lancer les tests à la main :

```bat
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Il y a deux sortes de tests :
- des tests de la ligne de commande ;
- deux tests de bout en bout, qui lancent une vraie capture et vérifient la taille et la couleur reçues avec le récepteur Spout `spout_probe`. Le test fenêtre ouvre une fenêtre, la ferme puis la rouvre.

## CI/CD

- **CI** : chaque commit poussé et chaque pull request est compilé et testé sur `windows-latest`. Le binaire produit est disponible en artefact.
- **Release** : pousser un tag `vX.Y.Z` compile, teste, puis publie une release GitHub avec `spout_stream-vX.Y.Z-win64.zip`.

  ```bat
  git tag v1.0.0
  git push origin v1.0.0
  ```

## Installation (portable)

`build.bat` produit le dossier `install\`, qui contient `spout_stream.exe`, ce README et la licence de Spout2. Pour déployer le programme sur un autre poste, copiez ce dossier où vous voulez. Aucune installation n'est nécessaire : le runtime C++ et Spout sont compilés dans l'exécutable, qui n'utilise que des DLL fournies avec Windows 10/11.

## Utilisation

```bat
spout_stream --list
spout_stream --screen                       :: écran principal
spout_stream --screen 1 --name "Ecran2"     :: écran n°1 (voir --list)
spout_stream --window "OBS" --name "OBS"    :: fenêtre dont le titre contient "OBS"
spout_stream --window "Jeu" --client --no-cursor
```

| Option | Rôle |
|---|---|
| `--screen [N]` | Capture l'écran N (index donné par `--list`). Sans N : écran principal. |
| `--window "titre"` | Capture la première fenêtre dont le titre contient `titre` (insensible à la casse). |
| `--list` | Liste les écrans et les fenêtres capturables. |
| `--name "nom"` | Nom du flux Spout (défaut : `SpoutStream`). |
| `--exact` | Le titre doit correspondre exactement. |
| `--client` | Ne diffuse que la zone client de la fenêtre (sans barre de titre ni bordures). |
| `--no-cursor` | N'inclut pas le curseur de la souris. |
| `--border` | Affiche le cadre jaune de capture de Windows (masqué par défaut sous Windows 11). |
| `--keep-last` | Garde la dernière image quand la source disparaît (par défaut, une image noire est envoyée). |
| `--poll MS` | Intervalle de recherche de la fenêtre (défaut : 500 ms). |
| `--version` | Affiche la version. |

### Comportement en mode fenêtre

- Si la fenêtre n'est pas encore ouverte, le programme l'attend.
- Si elle est fermée, le sender Spout reste actif (les récepteurs ne perdent pas la connexion) et affiche du noir. Le programme recherche alors la fenêtre et reprend la diffusion dès qu'elle revient.
- Une fois une fenêtre capturée, elle est suivie jusqu'à sa fermeture, même si son titre change.
- Si la fenêtre est redimensionnée, le flux Spout suit automatiquement la nouvelle taille.
- Une fenêtre réduite n'envoie plus d'images. Le flux reprend quand elle est restaurée.

Pour quitter, faites `Ctrl+C`. Le sender est alors libéré proprement.

## Remarques

- Windows 10 1903 ou plus récent est requis. Le masquage du cadre jaune nécessite Windows 11.
- Le format des pixels est BGRA 8 bits, le format natif Spout, sans conversion.
- Sur un portable à double GPU, les récepteurs Spout doivent tourner sur le même GPU que la capture pour éviter une copie entre GPU.

## Contribuer

Voir [CONTRIBUTING.md](CONTRIBUTING.md).

## Dépendances

[Spout2](https://github.com/leadedge/Spout2) (licence BSD 2 clauses) est compilé dans l'exécutable. Sa licence est livrée avec chaque version, dans `LICENSE-Spout2.txt`.
