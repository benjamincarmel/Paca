{
"name": "Build Paca Firmware & Web Flasher",
"on": {
"push": {
"branches": ["main", "master"]
},
"workflow_dispatch": {}
},
"permissions": {
"contents": "read",
"pages": "write",
"id-token": "write"
},
"concurrency": {
"group": "pages",
"cancel-in-progress": false
},
"jobs": {
"build": {
"runs-on": "ubuntu-latest",
"steps": [
{
"name": "Checkout repository",
"uses": "actions/checkout@v4"
},
{
"name": "Set up Python",
"uses": "actions/setup-python@v5",
"with": {
"python-version": "3.11"
}
},
{
"name": "Install Pillow and esptool",
"run": "python3 -m pip install --upgrade pillow esptool"
},
{
"name": "Stage sketch folder and sanitize files",
"run": "mkdir -p build/paca && cp ([ -f paca/paca.ino ] && echo paca/paca.ino || echo paca.ino) build/paca/paca.ino && ([ ! -f sprites.h ] || cp sprites.h build/paca/sprites.h) && ([ ! -f paca/sprites.h ] || cp paca/sprites.h build/paca/sprites.h) && ([ ! -f make_sprites.py ] || cp make_sprites.py build/make_sprites.py) && ([ ! -f paca/make_sprites.py ] || cp paca/make_sprites.py build/make_sprites.py) && ([ ! -f Paca.png ] || cp Paca.png build/Paca.png) && ([ ! -f paca/Paca.png ] || cp paca/Paca.png build/Paca.png) && ([ ! -f index.html ] || cp index.html build/index.html) && python3 -c 'import pathlib,sys; [p.write_text(chr(10).join(l for l in p.read_text().splitlines() if not l.strip().startswith(chr(96)*3))+chr(10)) for p in map(pathlib.Path,sys.argv[1:]) if p.exists()]' build/paca/paca.ino build/paca/sprites.h build/make_sprites.py build/index.html"
},
{
￼
￼
},
{
￼
￼
},
{
￼
￼
},
{
￼
￼
},
{
￼
￼(find ~/.arduino15 -name boot_app0.bin \vert{} head -n 1); python3 -m esptool --chip esp32s3 merge_bin -o out/paca-sticks3-merged.bin --flash_mode dio --flash_freq 80m --flash_size 8MB 0x0 out/paca.ino.bootloader.bin 0x8000 out/paca.ino.partitions.bin 0xe000￼
},
{
￼
￼
"with": {
￼
￼
}
},
{
￼
"run": "mkdir -p site && cp out/paca-sticks3-merged.bin site/paca-sticks3-merged.bin && ([ ! -f build/index.html ] || cp build/index.html site/index.html) && python3 -c 'import json,pathlib,sys; pathlib.Path(sys.argv[1]).write_text(json.dumps(dict(name=sys.argv[2],version=sys.argv[3],new_install_prompt_erase=True,builds=[dict(chipFamily=sys.argv[4],parts=[dict(path=sys.argv[5],offset=0)])])))' site/manifest.json 'Paca for M5Stack StickS3' 1.0.0 ESP32-S3 paca-sticks3-merged.bin"
},
{
￼
￼
"with": {
￼
}
}
￼
},
"deploy": {
￼
￼
"environment": {
￼
￼{{ steps.deployment.outputs.page_url }}"
},
"steps": [
{
"name": "Deploy web flasher to GitHub Pages",
"id": "deployment",
"uses": "actions/deploy-pages@v4"
}
]
}
}
}
