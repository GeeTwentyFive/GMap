![](_assets/GMap.webp)

Intended to be used either directly for new games/engines by reading from output JSON, or by feeding JSON into custom converter program which would convert it to an already existing map file format for an existing game/engine


# Usage

Create `GMap.json` with desired MapObjects -> run `GMap`

On exit: JSON of current map/scene is outputted to stdout (you can compose this with other programs/scripts (or just pipe to file: `./GMap > MAP_NAME.json`))

You can pass path to map/scene JSON as first CLI arg to `GMap` to load it


# GMap.json (MapObject registration)

```json
[
        {  // (only required field for each object is "model" (OBJ format))
                "model": "PATH/TO/MODEL.obj",  // (optional texture has to be PNG with same base name in same dir as model)
                "name": "",  // (default = model file base name)
                "data": [],  // one or more of: "position", "rotation", "scale", "color"
                "extra_data": {  // (also appears in inspector) (bool, number, string)
                        "example_1": 7.27,
                        "example_2": false
                }
        },
        {
                // ...^ repeat like above for 2nd MapObject...
        },
        // ...
]
```


# Controls

- While right mouse button is held down:
	- Mouse = rotate camera
	- WASD, Space, CTRL = move camera
	- Scroll = change camera move speed
	- Click = select/deselect object
	- 0 = reset camera position to 0, 0, 0
- Alt+D = duplicate selected object
- Del = delete selected object
- Alt+H = Hide/Show UI