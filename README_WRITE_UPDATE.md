# IMPLÉMENTATION D'UN PROTOCOLE DE WRITE_UPDATE_NOMMÉ ATOMIC_INC

## Contexte

  Ce fichier a pour but de présenter comment est implémenté le protocole **ATOMIC_INC** dans ce noyau selon la chronologie suivante :
  
  - [Présentation du sujet] (#Présentation-du-sujet)
  - [Étapes] (#Étapes)
  - [Envoi de l'hypercall] (#Envoi-de-l'hypercall)
  - [Réception de l'hypercall] (#Réception-de-l'hypercall)
  - [Mise à jour locale (méthode 2)] (#Mise-à-jour-locale-(méthode-2))
  - [Propagation de la mise à jour] (#Propagation-de-la-mise-à-jour)
  - [Mise à jour distante] (#Mise-à-jour-distante) 
  
  ## Présentation du sujet
