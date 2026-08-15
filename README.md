![](_assets/GMap.webp)

WIP

Intended to be used either directly for new games by reading from output JSON, or by feeding JSON into custom converter program which would convert it to an already existing map file format for an existing game/engine


# Usage

Run `GMap` to generate `GMap.json` -> edit `GMap.json` to register MapObjects

You can pass path to map/scene JSON as first CLI arg to load it.

On exit: JSON of current map/scene is outputted to stdout (you can compose this with other programs/scripts (or just pipe to file: `./GMap > MAP_NAME.json`))


# MapObject registration

```json
{
        "GLOBAL_SETTINGS": {/* ... */},
        "OBJECTS": [
                {  // (only required field for each object is "model" (OBJ format))
                        "model": "PATH/TO/MODEL.obj",  // (optional texture has to be PNG with same base name in same dir as model)
                        "name": "",  // (default = model base name)
                        "data": [],  // one or more of: "position", "rotation", "scale", "color"
                        "extra_data": {  // (appears in inspector) (null, bool, number, string)
                                "example_1": 7.27,
                                "example_2": false
                        }
                },
                {
                        // ...^ repeat like above for 2nd MapObject...
                },
                // ...
        ]
}
```


# Controls

- While right mouse button is held down:
	- Mouse = rotate camera
	- WASD, Space, CTRL = move camera
	- Scroll = change camera move speed
	- Click = select/deselect MapObject instance
	- 0 = reset camera position to 0, 0, 0
- Alt+D = duplicate selected MapObject instance
- Del = delete selected MapObject instance