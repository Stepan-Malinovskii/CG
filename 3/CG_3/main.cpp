#include "model.h"
#include "our_gl.h"
#include "geometry.h"

Model* model = NULL;
const int width = 800;
const int height = 800;

Vec3f light_dir = Vec3f(1, 1, 1).normalize();
Vec3f eye(1, 1, 3);
Vec3f center(0, 0, 0);
Vec3f up(0, 1, 0);

struct BaseShader : IShader
{
	Vec3f varying_intensity;
	mat<2, 3, float> varying_uv;

	virtual Vec4f vertex(int iface, int nthvert)
	{
		varying_uv.set_col(nthvert, model->uv(iface, nthvert));
		varying_intensity[nthvert] = std::max(0.0f, model->normal(iface, nthvert) * light_dir);
		Vec4f gl_Vertex = embed<4>(model->vert(iface, nthvert));
		return Viewport * Projection * ModelView * gl_Vertex;
	}

	virtual bool fragment(Vec3f bar, TGAColor& color)
	{
		float intensity = varying_intensity * bar;
		Vec2f uv = varying_uv * bar;
		color = model->diffuse(uv) * intensity;
		return false;
	}
};

struct PhongShader : IShader
{
    mat<3, 3, float> varying_n;
    mat<3, 3, float> varying_pos;
    mat<2, 3, float> varying_uv;

    Vec3f light_pos;
    Vec3f camera_pos;

    PhongShader(Vec3f _light_pos, Vec3f _camera_pos)
        : light_pos(_light_pos), camera_pos(_camera_pos)
    {
    }

    virtual Vec4f vertex(int iface, int nthvert) override
    {
        Vec3f world_pos = model->vert(iface, nthvert);
        Vec3f normal = model->normal(iface, nthvert);

        Vec4f world_pos_h = embed<4>(world_pos, 1.f);
        Vec4f view_pos_h = ModelView * world_pos_h;
        Vec4f clip_pos = Projection * view_pos_h;
        Vec4f screen_pos = Viewport * clip_pos;

        Vec4f normal_h = embed<4>(normal, 0.f);
        Vec3f view_normal = proj<3>(ModelView * normal_h);
        view_normal.normalize();

        Vec3f view_pos = proj<3>(view_pos_h);

        varying_uv.set_col(nthvert, model->uv(iface, nthvert));
        varying_n.set_col(nthvert, view_normal);
        varying_pos.set_col(nthvert, view_pos);

        return screen_pos;
    }

    virtual bool fragment(Vec3f bar, TGAColor& color) override
    {
        Vec3f n = (varying_n * bar).normalize();
        Vec3f frag_pos = varying_pos * bar;
        Vec2f uv = varying_uv * bar;

        Vec4f light_pos_world_h = embed<4>(light_pos, 1.f);
        Vec3f light_pos_view = proj<3>(ModelView * light_pos_world_h);

        Vec4f camera_pos_world_h = embed<4>(camera_pos, 1.f);
        Vec3f camera_pos_view = proj<3>(ModelView * camera_pos_world_h);

        Vec3f light_dir = (light_pos_view - frag_pos).normalize();
        Vec3f view_dir = (camera_pos_view - frag_pos).normalize();

        Vec3f reflect_dir = (n * (n * light_dir) * 2.0f - light_dir).normalize();

        float ambient = 0.5f;
        float diffuse = std::max(0.0f, n * light_dir);
        float shininess = 50.0f;
        float specular = std::pow(std::max(0.0f, reflect_dir * view_dir), shininess);

        TGAColor modelColor = model->diffuse(uv);
        Vec3f base_color = Vec3f(modelColor[2], modelColor[1], modelColor[0]) / 255.0f;
        Vec3f result = base_color * (ambient + diffuse) + Vec3f(1, 1, 1) * specular;

        color = TGAColor(
            std::min(255, (int)(result.x * 255)),
            std::min(255, (int)(result.y * 255)),
            std::min(255, (int)(result.z * 255))
        );

        return false;
    }
};

struct Wireframe : IShader
{
    const float edge_threshold = 0.04f;

    virtual Vec4f vertex(int iface, int nthvert) override
    {
        Vec4f gl_Vertex = embed<4>(model->vert(iface, nthvert));
        return Viewport * Projection * ModelView * gl_Vertex;
    }

    virtual bool fragment(Vec3f bar, TGAColor& color) override
    {
        if (bar.x < edge_threshold || bar.y < edge_threshold || bar.z < edge_threshold)
        {
            color = TGAColor(255, 255, 255);   
        }
        else
        {
            color = TGAColor(0, 0, 0);
        }
        return false;
    }
};

int main()
{
	model = new Model("res/african_head.obj");

	lookat(eye, center, up);
	viewport(width / 8, height / 8, width * 3 / 4, height * 3 / 4);
	projection(-1.0f / (eye - center).norm());
	
	TGAImage image(width, height, TGAImage::RGB);
	TGAImage zbuffer(width, height, TGAImage::GRAYSCALE);

    Wireframe shader;
	for (int i = 0; i < model->nfaces(); i++)
	{
		Vec4f screen_coords[3];
		for (int j = 0; j < 3; j++)
		{
			screen_coords[j] = shader.vertex(i, j);
		}
		triangle(screen_coords, shader, image, zbuffer);
	}

	image.flip_vertically();
	image.write_tga_file("output.tga");
	//image.ShowImage("output.tga");

	delete model;
	return 0;
}