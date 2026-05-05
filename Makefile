# Nom de l'archive finale (Adaptez avec vos noms)
STUDENT_NAME=PROJET_INFO9012
ARCHIVE_NAME=$(STUDENT_NAME).tar.gz

# Dossiers à inclure
SUBDIRS=sequential 1 2 3 4

.PHONY: all clean archive

all:
	@echo "Utilisez 'make archive' pour générer le fichier de soumission."

# Règle pour nettoyer tous les dossiers avant l'envoi
# On évite d'envoyer les fichiers .o et l'exécutable 'raycaster'
clean:
	@for dir in $(SUBDIRS); do \
		echo "Nettoyage de $$dir..."; \
		rm -f $$dir/raycaster $$dir/*.o; \
	done

# Règle pour créer l'archive de soumission
archive: clean
	@echo "Création de l'archive $(ARCHIVE_NAME)..."
	tar -cvzf $(ARCHIVE_NAME) $(SUBDIRS) README.md
	@echo "--------------------------------------------------"
	@echo " Archive générée avec succès : $(ARCHIVE_NAME)"
	@echo " Vérifiez son contenu avant de la soumettre sur eCampus."
	@echo "--------------------------------------------------"