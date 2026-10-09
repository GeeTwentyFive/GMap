![](_assets/GMap.webp)

Intended to be used either directly for new games by reading from output JSON, or by feeding JSON into custom converter program which would convert it to an already existing map file format for an existing game


# Usage

Run `GMap` and pipe a MapObject registry (JSON array of JSON objects) into its stdin (e.g. via `|`, `<`, OS API syscalls, ...)

On exit: JSON of current map/scene is outputted to stdout (you can compose this with other programs/scripts (or just pipe to file: `> MAP_NAME.json`))

You can pass path to map/scene JSON file as first CLI arg to `GMap` to load it


# MapObject registry format

```json
[
        {  // (only required field for each object is "model" (OBJ format))
                "model": "PATH/TO/MODEL.obj",  // (optional texture has to be PNG with same base name in same dir as model ('PATH/TO/MODEL.png'))
                "data": [],  // one or more of: "position", "rotation", "scale", "color", "user_model", "user_texture"
                "extra_data": {  // (also appears in inspector) (bool, number, string)
                        "example_1": true,
                        "example_2": 7.27,
                        "example_3": "Hello, World!"
                }
        },
        {
                // ...^ repeat like above for 2nd MapObject...
        },
        // ...
]
```
- `"model"`: Path to base/default model for MapObject. Is also used to identify MapObject. If a PNG texture with the same base name as the model in the same directory exists; it is used as the texture for the model. Base name of model file is used as its label in GMap's GUI. It is **always** included in the output.
- `"data"`: Which data (with visual scene support) to include in inspector (where the user can modify it) and the output. Must be one or more of: `"position"`, `"rotation"`, `"scale"`, `"color"`, `"user_model"`, `"user_texture"`. `"user_model"` and `"user_texture"` allow the user to select a custom model and texture (respectively) for each instance of the MapObject during runtime.
- `"extra_data"`: Additional data (without visual scene support) to include in inspector and output (value of each member must be one of: `bool`, `float`, `int`, `uint`, `string`)


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