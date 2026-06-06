Planet rings
	These are around the planet. They should at least by default be at the planets equator. I want to have some color picker, that allows me to choose the color of the rings such that each on a line, I define points and colors for those points, and then we add gradients between the points, and then we turn the line into a ring. 
	
Textures
	I want these to be more complex. I want to be able to vary multiple variables. Also, I think that the objects would look better, if they were icospheres.
	
Lighting
	Currently, I think that the lighting comes from a constant direction. My vision was, that we get some ambient backround lighting, and then the rest of the light comes from the sun(s).
	
	
Planet details
	I want to be able to add to the planets something analogous to athmosphere. That means, that there is a small sphere on top of the planet, that is translucent with some scatter. I was also wondering, if we could add realism in how the color of the light changes, when the scatter happens. But maybe this wont add anything, idk. 


Rendering
	The style is currently something, that looks like this is some early 90s videogame. Which is understandable, you have been just following my instructions. But I was imagining, that the final result would be something more artistic. One idea how to achieve this was that we could limit the pallet somehow. I am not an expert, but in good pixel space art, the selection of colors seems to be crucial. Like having some harmonic color pallet seems to help. This is easy to get wrong, and thus I want to have tools, that I can edit in real time. I was thinking, that the way to do this would be something like:
	Define a color pallet. All colors used must be there
	Apply a smoothing filter over the image
	round pixels to nearest color from pallet (random probably better than deterministic, but don't know
	Possibly reapply a few times?
	
	
	
Background
	I want to have a starry background. I would be cool if we could use some like real nightsky hdr, but that might be ambitious, idk. Alternatively, se could just have some generator that trickles stars around, like some texture, where I can edit the parameters.