# Contribuer

Les propositions d'amélioration sont les bienvenues.

- **Signaler un bug ou proposer une idée** : ouvrez une *issue*. Précisez votre version de Windows, la commande utilisée et la sortie console.
- **Proposer une modification** :
  1. faites un *fork* du dépôt et créez une branche ;
  2. compilez et lancez les tests :
     ```bat
     cmake -S . -B build -A x64
     cmake --build build --config Release
     ctest --test-dir build -C Release --output-on-failure
     ```
  3. ouvrez une *pull request* vers `main` en décrivant le changement.

Chaque pull request est compilée et testée automatiquement par la CI. Elle est fusionnée après relecture et approbation du mainteneur.

Les failles de sécurité se signalent en privé, via l'onglet *Security* > *Report a vulnerability*, et non dans une issue publique.
