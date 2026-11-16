# ROMANCE

Synthétiseur digital dans l'esprit Rompleur d'époque à ROM générée, en C++ / JUCE. Nom provisoire.

## L'idée

Je souhaitais m'essayer à faire un synthé au comportement similaire à Pigments, Serum ou autres synthétiseurs numériques actuels, tout en ayant la possibilité de glisser un son dans le plugin pour le resynthétiser et pouvoir modifier celui-ci par la suite : 

- une recherche resynthétise le son au plus proche, dans la limite de ses capacités. Au lieu de lire un échantillon, l'instrument reconstruit une table d'onde et les paramètres qui s'en approchent le mieux.

Ce projet en est à ses débuts car c'est un sacré chantier ^^ ; je pense continuer à pousser l'idée dans le futur, en réduisant la portée de la V1, car les paramètres à améliorer sont trop nombreux actuellement. Le prototype est utilisable et assez intéressant soniquement, mais il est assez gourmand en CPU, l'UI/UX est encore très sommaire et de nombreux bugs ou imprécisions dans l'utilisation sont malheureusement présents.

J'ai aussi voulu émuler la quantification et les limitations de reproduction et de fidélité sonores des machines des années 70 à 90 pour avoir une expérience « low fidelity » assumée, qui reste optionnelle.


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
