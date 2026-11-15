# ROMANCE

Rompleur d'époque à ROM générée, en C++ / JUCE. Nom provisoire.

## L'idée

On glisse un son dans le plugin ; une recherche hors-ligne fabrique le matériau d'onde et les
réglages qui le reproduisent. Au lieu de lire un échantillon, l'instrument reconstruit une
table d'onde et les paramètres qui s'en approchent le mieux.

## Contenu

| Dossier | Rôle |
|---|---|
| `ROMANCE/` | le plugin : source C++, projet JUCE |
| `_proto/` | prototype Python : recherche, synthèse, rendus de contrôle |

Le corpus d'écoute, les rendus audio et les manuels de référence sont exclus du dépôt.

## État

Prototype en cours. La chaîne de recherche fonctionne ; l'intégration dans le plugin est en
chantier.

## Construire

Ouvrir `ROMANCE/ROMANCE.jucer` dans Projucer, puis compiler sous Visual Studio 2022.
Cible : VST3, Windows 64 bits. Validation avec [pluginval](https://github.com/Tracktion/pluginval).

## Note

Conception et validation sont de moi. L'écriture du code s'appuie sur une assistance IA.

## Droits

© Lucas Lallier-Ginoux. **Tous droits réservés.** Ce dépôt est publié en consultation
uniquement, comme portfolio. Aucune licence d'utilisation, de modification, de redistribution
ou d'exploitation commerciale n'est accordée.
