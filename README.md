Intended to be used either directly for new games by reading from output JSON, or by feeding JSON into custom converter program which would convert it to an already existing map file format for an existing game/engine


# Usage

TODO


# MapObject registration

```json
{
        "GLOBAL_SETTINGS": {/* ... */},
        "OBJECTS": [
                {  // (only required field for each object is "model")
                        "model": "PATH/TO/MODEL.obj",  // (texture has to be PNG with same base name)
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