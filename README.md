# MotionToGo

MotionToGo is a demonstration of adding motion blur to a video or image sequence. It uses GPU's motion estimator to estimate the motion vectors, then use a post process to apply the motion blur to the sequence. It can be used to increase the smoothness of stop motion video, make it looks like go motion (That's why it's called motion to go. Applying motion later.). Also, AI generated videos are too crispy clear. Adding some motion blur can make them look more real.

The code is mostly assembled from old code, nothing new. All components come from existing resources. The motion estimator is based on [New in D3D12 ¨C Motion Estimation](https://devblogs.microsoft.com/directx/new-in-d3d12-motion-estimation/). The motion blur algorithm is from [Scalable High Quality Motion Blur and Ambient Occlusion Scalable High Quality Motion Blur and Ambient Occlusion](https://advances.realtimerendering.com/s2012/Vicarious%20Visions/Vicarious%20Visions%20Siggraph%202012.pdf). The video decoding is based on [Using the Source Reader to Process Media Data](https://learn.microsoft.com/en-us/windows/win32/medfound/processing-media-data-with-the-source-reader).

## Prerequisites

* [Visual Studio 2022](https://www.visualstudio.com/downloads)
* [CMake](https://www.cmake.org/download/)

## License

MotionToGo is distributed under the terms of MIT License. See [LICENSE](LICENSE) for details.
