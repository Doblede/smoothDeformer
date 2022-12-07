# smoothDeformer
An Autodesk Maya deformer that implements the Laplacian and Taubin smoothing algorithms.
The deformer is multithreading, running in as many threads as number of cores the machine has.

Laplacian smoothing algorithm doesn't maintain the volume ov the mesh, but a "maintain volume" attribute was added.

To create the deformer just run
```python
cmds.deformer(type='smoothDeformer')
```