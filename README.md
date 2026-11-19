# ROMANCE

Synthétiseur digital dans l'esprit Rompleur d'époque à ROM générée, en C++ / JUCE. Nom provisoire.

## L'idée

Je souhaitais m'essayer à faire un synthé au comportement similaire à Pigments, Serum ou autres synthétiseurs numériques actuels, tout en ayant la possibilité de glisser un son dans le plugin pour le resynthétiser et pouvoir modifier celui-ci par la suite : 

- une recherche resynthétise le son au plus proche, dans la limite de ses capacités. Au lieu de lire un échantillon, l'instrument reconstruit une table d'onde et les paramètres qui s'en approchent le mieux.

Ce projet en est à ses débuts car c'est un sacré chantier ^^ ; je pense continuer à pousser l'idée dans le futur, en réduisant la portée de la V1, car les paramètres à améliorer sont trop nombreux actuellement. Le prototype est utilisable et assez intéressant soniquement, mais il est assez gourmand en CPU, l'UI/UX est encore très sommaire et de nombreux bugs ou imprécisions dans l'utilisation sont malheureusement présents.

J'ai aussi voulu émuler la quantification et les limitations de reproduction et de fidélité sonores des machines des années 70 à 90 pour avoir une expérience « low fidelity » assumée, qui reste optionnelle.


## Validation

**377 contrôles automatisés** dans `tests/`, et le point qui compte : leurs bornes ne sont pas
choisies à la main, elles viennent des valeurs mesurées sur le prototype Python. Le prototype
mesure, le test C++ vérifie que le portage reste dans ces bornes. Sans ça, un test ne fait que
confirmer ce que le code fait déjà.

## Quatre pièges de banc rencontrés ici

Consignés pour ne pas les redécouvrir. Ils valent pour n'importe quelle mesure audio.

1. **La mesure d'un bruit est elle-même bruitée.** Une détection de coupure à −3 dB sans lissage
   donnait des résultats non monotones : une coupure qui descend quand on transpose vers le haut.
   Un filtre médian de 31 points a suffi. Toujours vérifier la monotonie d'une mesure censée
   être monotone.
2. **Un test d'artefacts sans témoin ne mesure rien.** La première version affichait −50 dB
   partout, et j'ai failli en conclure quelque chose. Le témoin — un sinus pur synthétisé
   directement — est à −59 dB : c'est le plancher de *ma fenêtre d'analyse*, pas celui de la
   machine. Sans lui, 9 dB de marge auraient été lus comme 50.
3. **Vers le grave, le test ne discrimine pas.** Transposer vers le bas ramène le contenu sous la
   coupure : c'est la ROM qui limite, pas le filtre. Seule la transposition vers le haut éprouve
   réellement le filtre asservi.
4. **Ne pas renormaliser les rendus.** Ils sortent déjà alignés à 0,01 dB près en loudness K. Une
   passe de plus n'ajoute que de l'erreur.

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
