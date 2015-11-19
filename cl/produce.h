#pragma once

#include "opencl.h"

#include "ray.h"
#include "hit.h"
#include "hit_info.h"
#include "random.h"

#define DELTA 1e-6f

float3 get_sky_color(float3 dir)
{
	return (float3)(0.6f,0.6f,0.8f)*((float3)(dir.z,dir.z,dir.z)*0.5f + (float3)(0.5f,0.5f,0.5f));
	//return (float3)((int)(10*(dir.x + 1))%2,(int)(10*(dir.y + 1))%2,(int)(10*(dir.z + 1))%2);
	//return (float3)(0.0f,0.0f,0.0f);
}

float3 reflect(float3 dir, float3 norm)
{
	return dir - 2.0f*norm*dot(dir,norm);
}

float3 diffuse(float3 norm, uint *seed)
{
	float3 nx, ny;
	if(dot((float3)(0.0f,0.0f,1.0f),norm) < 0.6 && dot((float3)(0.0f,0.0f,1.0f),norm) > -0.6)
	{
		nx = (float3)(0.0f,0.0f,1.0f);
	}
	else
	{
		nx = (float3)(1.0f,0.0f,0.0f);
	}
	ny = normalize(cross(nx,norm));
	nx = cross(ny,norm);

	float phi = 2.0f*M_PI_F*random_unif(seed);
	float theta = acos(1.0f - 2.0f*random_unif(seed))/2.0f;
	return nx*cos(phi)*sin(theta) + ny*sin(phi)*sin(theta) + norm*cos(theta);
}

float3 reflect_diffused(float3 dir, float3 norm, float factor, uint *seed)
{
	float3 dif = diffuse(norm,seed);
	float3 ref = reflect(dir,norm);
	float par = dot(dif,norm);
	float3 ort = dif - par*norm;
	return normalize(factor*par*ref + ort);
}

float3 get_dir_to_obj(float3 src, global const float *obj, float *f, uint *seed) {
	float3 dst = (float3) (0.0f, 0.0f, 0.0f);
	int i;
	for(i = 0; i < 6; ++i) {
		dst += vload3(i, obj);
	}
	dst /= 6.0f;
	float max_dist2 = 0.0f;
	for(i = 0; i < 6; ++i) {
		float3 vd = vload3(i, obj) - src;
		float dist2 = dot(vd, vd);
		if(dist2 > max_dist2) {
			max_dist2 = dist2;
		}
	}
	float len = length(dst - src);
	*f = 0.02;//0.5*max_dist2/(len*len);
	float3 dir = (dst - src)/len;
	return dir;
}

__kernel void produce(
	__global const uchar *hit_data, __global uchar *ray_data, __global const uchar *hit_info,
	__global const float *lights,
	__global uint *color_buffer, const uint pitch, const uint work_size,
	__global uint *random
)
{
	const int size = get_global_size(0);
	const int pos = get_global_id(0);
	
	if(pos >= work_size)
	{
		return;
	}
	
	const float3 diff[4] = {{0.2f,0.2f,0.6f},{0.2f,0.2f,0.0f},{0.0f,1.0f,0.0f},{0.0f,0.0f,0.0f}};
	const float3 refl[4] = {{0.4f,0.4f,0.4f},{0.8f,0.8f,0.2f},{0.0f,0.0f,0.0f},{0.8f,0.8f,0.8f}};
	const float3 glow[4] = {{0.0f,0.0f,0.0f},{0.0f,0.0f,0.0f},{0.0f,0.0f,0.0f},{32.0f,32.0f,24.0f}};
	
	Hit hit = hit_load(pos,hit_data);
	
	float3 color = {0.0f,0.0f,0.0f};
	
	// glowing
	if(hit.object == 0)
	{
		color += hit.color*get_sky_color(hit.dir);
	}
	else
	{
		color += hit.color*glow[hit.object-1];
	}
	
	HitInfo info = hit_info_load(pos,hit_info);
	
	if(info.size > 0)
	{
		uint seed = random[pos];
		
		Ray ray;
		ray.pos = hit.pos + hit.norm*DELTA;
		
		ray.origin = hit.origin;
		ray.source = hit.object;
		ray.target = 0;
		
		uint count = 0;
		
		// reflection
		if(info.pre_size.x)
		{
			ray.color = hit.color*refl[hit.object-1];
			if(hit.object-1 == 1)
			{
				ray.dir = reflect_diffused(hit.dir,hit.norm,8.0f,&seed);
			}
			else
			{
				ray.dir = reflect(hit.dir,hit.norm);
			}
			ray_store(&ray, info.offset + count, ray_data);
			++count;
		}
		
		// diffusion
		float3 color = hit.color*diff[hit.object-1]/(info.size-count);
		
		// diffuse light attraction
		/*
		{
			float f;
			ray.dir = get_dir_to_obj(hit.pos, lights, &f, &seed);
			ray.color = f*color;
			if(dot(ray.dir, hit.norm) > 0.0f) {
				ray.target = 4;
				ray_store(&ray, info.offset + count, ray_data);
				++count;
			}
		}
		*/
		
		// random direction rays
		ray.color = color;
		for(; count < info.size; ++count)
		{
			ray.dir = diffuse(hit.norm,&seed);
			ray.target = 0;
			ray_store(&ray,info.offset + count,ray_data);
		}
		
		random[pos] = seed;
	}
	
	// replace with atomic_add for float in later version
	atomic_add(color_buffer + 3*(hit.origin.x + hit.origin.y*pitch) + 0, (uint)(0x10000*color.x));
	atomic_add(color_buffer + 3*(hit.origin.x + hit.origin.y*pitch) + 1, (uint)(0x10000*color.y));
	atomic_add(color_buffer + 3*(hit.origin.x + hit.origin.y*pitch) + 2, (uint)(0x10000*color.z));
	//vstore3(convert_uint4(0x10000*color) + vload3(hit.origin.x + hit.origin.y*pitch,color_buffer),hit.origin.x + hit.origin.y*pitch,color_buffer);
}