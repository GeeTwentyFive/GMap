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
                        "extra_data": [  // (appears in inspector)
                                {"example_1": 7.27},
                                {"example_2": false}
                        ],
                        "override_translate": false,  // (precedence over global)
                        "override_rotate": false,  // (precedence over global)
                        "override_scale": false,  // (precedence over global)
                        "override_bounds": false,  // (precedence over global)
                        "override_coloring": false,  // (precedence over global)
                        "singleton": false  // (already-present, just one, non-removable)
                },
                {
                        // ...^ repeat like above for 2nd MapObject...
                },
                // ...
        ]
}
```