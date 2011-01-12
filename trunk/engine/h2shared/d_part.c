/*
	d_part.c
	software driver module for drawing particles

	$Id$
*/

#include "quakedef.h"
#include "d_local.h"


/*
==============
D_EndParticles
==============
*/
void D_EndParticles (void)
{
// not used by software driver
}


/*
==============
D_StartParticles
==============
*/
void D_StartParticles (void)
{
// not used by software driver
}


#if	!id386

/*
==============
D_DrawParticle
==============
*/
void D_DrawParticle (particle_t *pparticle)
{
	vec3_t	local, transformed;
	float	zi;
	byte	*pdest;
	short	*pz;
	int		i, izi, pix, count, u, v;
	int		Color;	// Crusader's ice particles hitting a wall gives us
				// -2 as the color, causing a big boom.  therefore,
				// use signed int here and clamp to 0-511 as needed.
	qboolean	NoTrans;

// transform point
	VectorSubtract (pparticle->org, r_origin, local);

	transformed[0] = DotProduct(local, r_pright);
	transformed[1] = DotProduct(local, r_pup);
	transformed[2] = DotProduct(local, r_ppn);		

	if (transformed[2] < PARTICLE_Z_CLIP)
		return;

// project the point
// FIXME: preadjust xcenter and ycenter
	zi = 1.0 / transformed[2];
	u = (int)(xcenter + zi * transformed[0] + 0.5);
	v = (int)(ycenter - zi * transformed[1] + 0.5);

	if ((v > d_vrectbottom_particle) || 
		(u > d_vrectright_particle) ||
		(v < d_vrecty) ||
		(u < d_vrectx))
	{
		return;
	}

	pz = d_pzbuffer + (d_zwidth * v) + u;
	pdest = d_viewbuffer + d_scantable[v] + u;
	izi = (int)(zi * 0x8000);

	pix = izi >> d_pix_shift;

	if (pix < d_pix_min)
		pix = d_pix_min;
	else if (pix > d_pix_max)
		pix = d_pix_max;

	Color = (int)pparticle->color;
	if (Color < 0)				// try to keep 0-511, see above
		Color += 511;
//	if (Color < 0 || Color > 511)
//		Sys_Error ("%s: Bad color %d", __thisfunc__, Color);
	NoTrans = (Color <= 255) ? true : false;
	if (NoTrans == false)
		Color = (Color - 256) << 8;	// will use as transTable index

	switch (pix)
	{
	case 1:
		count = 1 << d_y_aspect_shift;

		for ( ; count ; count--, pz += d_zwidth, pdest += screenwidth)
		{
			if (pz[0] <= izi)
			{
				pz[0] = izi;
				pdest[0] = (NoTrans) ? Color : transTable[Color + pdest[0]];
			}
		}
		break;

	case 2:
		count = 2 << d_y_aspect_shift;

		for ( ; count ; count--, pz += d_zwidth, pdest += screenwidth)
		{
			if (pz[0] <= izi)
			{
				pz[0] = izi;
				pdest[0] = (NoTrans) ? Color : transTable[Color + pdest[0]];
			}

			if (pz[1] <= izi)
			{
				pz[1] = izi;
				pdest[1] = (NoTrans) ? Color : transTable[Color + pdest[1]];
			}
		}
		break;

	case 3:
		count = 3 << d_y_aspect_shift;

		for ( ; count ; count--, pz += d_zwidth, pdest += screenwidth)
		{
			if (pz[0] <= izi)
			{
				pz[0] = izi;
				pdest[0] = (NoTrans) ? Color : transTable[Color + pdest[0]];
			}

			if (pz[1] <= izi)
			{
				pz[1] = izi;
				pdest[1] = (NoTrans) ? Color : transTable[Color + pdest[1]];
			}

			if (pz[2] <= izi)
			{
				pz[2] = izi;
				pdest[2] = (NoTrans) ? Color : transTable[Color + pdest[2]];
			}
		}
		break;

	case 4:
		count = 4 << d_y_aspect_shift;

		for ( ; count ; count--, pz += d_zwidth, pdest += screenwidth)
		{
			if (pz[0] <= izi)
			{
				pz[0] = izi;
				pdest[0] = (NoTrans) ? Color : transTable[Color + pdest[0]];
			}

			if (pz[1] <= izi)
			{
				pz[1] = izi;
				pdest[1] = (NoTrans) ? Color : transTable[Color + pdest[1]];
			}

			if (pz[2] <= izi)
			{
				pz[2] = izi;
				pdest[2] = (NoTrans) ? Color : transTable[Color + pdest[2]];
			}

			if (pz[3] <= izi)
			{
				pz[3] = izi;
				pdest[3] = (NoTrans) ? Color : transTable[Color + pdest[3]];
			}
		}
		break;

	default:
		count = pix << d_y_aspect_shift;

		for ( ; count ; count--, pz += d_zwidth, pdest += screenwidth)
		{
			for (i = 0 ; i < pix ; i++)
			{
				if (pz[i] <= izi)
				{
					pz[i] = izi;
					pdest[i] = (NoTrans) ? Color : transTable[Color + pdest[i]];
				}
			}
		}
		break;
	}
}

#endif	/* !id386 */
