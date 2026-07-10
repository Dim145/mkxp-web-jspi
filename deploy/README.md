# Déploiement — conteneur Docker du jeu

Le jeu est **100 % client-side** (WASM + assets). Le conteneur ne fait que servir
des fichiers statiques : aucun coût CPU serveur par joueur, nombre de joueurs
simultanés illimité. Image minimale (**Caddy** officiel, Alpine), à jour et durcie
(non-root, `read_only`, `cap_drop ALL`, `no-new-privileges`).

## Contenu

| Fichier | Rôle |
|---|---|
| `deploy/Dockerfile` | build multi-étages : (1) pré-compresse le texte/WASM en `.br`+`.gz`, (2) image Caddy non-root |
| `deploy/Caddyfile` | MIME `application/wasm`, Brotli/gzip pré-compressé, cache, en-têtes de sécurité |
| `deploy/docker-compose.yml` | lancement durci clé-en-main |
| `.dockerignore` (racine) | limite le contexte de build au jeu (`mkxp-web/build/`) |

> Prérequis : le jeu doit être bâti dans `mkxp-web/build/` (voir `rebuild-lto.sh` /
> `rebuild-jspi.sh` pour le moteur, `scripts_tool.rb` + `integrate-game.sh` pour les assets).

## Build & run

Depuis la **racine du dépôt** :

```bash
# build (contexte = racine, pour copier mkxp-web/build/)
docker build -f deploy/Dockerfile -t rgss-web .

# run durci (non-root déjà dans l'image)
docker run -d --name rgss-web \
  -p 8080:8080 \
  --read-only --tmpfs /tmp \
  -v rgss-web-caddy-data:/data -v rgss-web-caddy-config:/config \
  --cap-drop ALL --security-opt no-new-privileges \
  --restart unless-stopped \
  rgss-web
```

ou, plus simple, via compose :

```bash
cd deploy && docker compose up -d --build
```

Le jeu est alors sur `http://<hôte>:8080/`.

## HTTPS (obligatoire en prod)

Le **Service Worker** (chargement instantané + hors-ligne) exige un contexte
sécurisé : le jeu doit être servi en **HTTPS** (ou `localhost`). Deux options :

- **Derrière ton reverse proxy existant** (Apache/Nginx, recommandé, conforme à
  l'archi du projet) : le conteneur reste en HTTP sur `:8080`, le proxy fait le TLS.
  Exemple Apache : `ProxyPass / http://127.0.0.1:8080/` + `ProxyPassReverse /`, et
  mets le HSTS au niveau du proxy. (Le `.htaccess` fourni sert pour un hébergement
  Apache statique *sans* ce conteneur — inutile ici.)
- **HTTPS autonome par Caddy** : dans `deploy/Caddyfile`, supprime `auto_https off`
  et remplace `:8080` par ton domaine (`play.example.com`). Caddy obtient et
  renouvelle un certificat Let's Encrypt tout seul. Publie alors les ports 80 + 443
  (`-p 80:80 -p 443:443`) et garde `/data` sur un volume (stockage des certificats).

## Ce que fait la config

- **MIME** : `.wasm` → `application/wasm` (active `WebAssembly.instantiateStreaming`).
- **Compression** : sert les `.br` (Brotli q11) / `.gz` pré-générés au build ; repli
  dynamique zstd/gzip. WASM ~4 Mo → ~1,1 Mo. Les médias (PNG/OGG) ne sont pas
  recompressés (déjà compressés).
- **Cache** : `immutable` 1 an pour le moteur (`?v=`) et les médias hachés (`?h=`) ;
  `no-cache` (revalidation) pour la coquille (`index.html`, `sw.js`, `mapping.js`, `js/*`).
- **Sécurité** : non-root, `nosniff`, `Referrer-Policy`, CSP `frame-ancestors`,
  bannière `Server` retirée, API admin Caddy désactivée. Pas de COOP/COEP (build
  mono-thread, inutile).

## Variante : ne pas embarquer les 389 Mo d'assets

Par défaut l'image est autonome (assets inclus, ~400 Mo). Pour une image minuscule
qui monte les assets depuis l'hôte, retire le `COPY … /srv` du `Dockerfile` et monte
le dossier en lecture seule au run :

```bash
docker run -d -p 8080:8080 -v "$PWD/mkxp-web/build:/srv:ro" rgss-web
```

(dans ce cas la pré-compression Brotli du build n'a pas lieu — génère les `.br`/`.gz`
à la main dans `mkxp-web/build/`, ou accepte le repli gzip dynamique de Caddy.)
