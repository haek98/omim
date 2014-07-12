#include "testing_engine.hpp"

#include "../coding/file_reader.hpp"
#include "../platform/platform.hpp"

#include "../drape/vertex_array_buffer.hpp"
#include "../drape/shader_def.hpp"

#include "../drape_frontend/visual_params.hpp"
#include "../drape_frontend/line_shape.hpp"
#include "../drape_frontend/area_shape.hpp"
#include "../drape_frontend/circle_shape.hpp"

#include "../geometry/transformations.hpp"

#include "../base/stl_add.hpp"
#include "../base/timer.hpp"

#include "../std/bind.hpp"
#include "../std/function.hpp"
#include "../std/vector.hpp"

#include "../../3party/jansson/myjansson.hpp"

namespace df
{

class MapShapeFactory
{
  typedef function<MapShape * (json_t *)> TCreateFn;
  typedef map<string, TCreateFn> TCreatorsMap;

public:
  MapShapeFactory()
  {
    m_creators["line"] = bind(&MapShapeFactory::CreateLine, this, _1);
    m_creators["area"] = bind(&MapShapeFactory::CreateArea, this, _1);
    m_creators["dyn_square"] = bind(&MapShapeFactory::CreateDynSquare, this, _1);
    m_creators["circle"] = bind(&MapShapeFactory::CreateCircle, this, _1);
  }

  void CreateShapes(vector<MapShape *> & shapes, json_t * object)
  {
    void * iter = json_object_iter(object);
    while(iter)
    {
      json_t * entry = json_object_iter_value(iter);
      if (entry)
      {
        string const type(json_object_iter_key(iter));
        if (type != "_comment_")
        {
          TCreatorsMap::const_iterator it = m_creators.find(type);
          ASSERT(it != m_creators.end(), ());
          shapes.push_back(it->second(entry));
        }
        iter = json_object_iter_next(object, iter);
      }
    }
  }

private:
  Color ParseColor(json_t * object)
  {
    size_t const channelCount = json_array_size(object);
    ASSERT(channelCount == 4, ());
    int const r = json_integer_value(json_array_get(object, 0));
    int const g = json_integer_value(json_array_get(object, 1));
    int const b = json_integer_value(json_array_get(object, 2));
    int const a = json_integer_value(json_array_get(object, 3));
    return Color(r, g, b, a);
  }

  float ParseCoord(json_t * object)
  {
    if (json_is_real(object))
      return json_real_value(object);
    else if (json_is_integer(object))
      return json_integer_value(object);

    ASSERT(false, ());
    return 0.0f;
  }

  void ParseGeometry(json_t * object, vector<m2::PointF> & points)
  {
    size_t const count = json_array_size(object);
    ASSERT((count & 1) == 0, ());
    points.reserve(count >> 1);
    for (size_t i = 0; i < count; i += 2)
    {
      double const x = ParseCoord(json_array_get(object, i));
      double const y = ParseCoord(json_array_get(object, i + 1));
      points.push_back(m2::PointF(x, y));
    }
  }

  df::LineJoin ParseJoin(json_t * object)
  {
    return (df::LineJoin)json_integer_value(object);
  }

  df::LineCap ParseCap(json_t * object)
  {
    return (df::LineCap)json_integer_value(object);
  }

  MapShape * CreateLine(json_t * object)
  {
    LineViewParams params;
    params.m_depth = json_real_value(json_object_get(object, "depth"));
    params.m_color = ParseColor(json_object_get(object, "color"));
    params.m_width = json_real_value(json_object_get(object, "width"));
    params.m_join = ParseJoin(json_object_get(object, "join"));
    params.m_cap = ParseCap(json_object_get(object, "cap"));

    vector<m2::PointF> points;
    ParseGeometry(json_object_get(object, "geometry"), points);
    return new LineShape(points, params);
  }

  MapShape * CreateArea(json_t * object)
  {
    AreaViewParams params;
    params.m_depth = json_real_value(json_object_get(object, "depth"));
    params.m_color = ParseColor(json_object_get(object, "color"));
    vector<m2::PointF> points;
    ParseGeometry(json_object_get(object, "geometry"), points);

    return new AreaShape(points, params);
  }

  MapShape * CreateDynSquare(json_t * object)
  {
    /// TODO
    ASSERT(false, ());
    return NULL;
  }

  MapShape * CreateCircle(json_t * object)
  {
    CircleViewParams params(FeatureID(-1, 0));
    params.m_depth = json_real_value(json_object_get(object, "depth"));
    params.m_color = ParseColor(json_object_get(object, "color"));
    params.m_radius = json_real_value(json_object_get(object, "radius"));
    vector<m2::PointF> point;
    ParseGeometry(json_object_get(object, "geometry"), point);

    return new CircleShape(point[0], params);
  }

private:
  TCreatorsMap m_creators;
};

class SquareHandle : public OverlayHandle
{
public:
  static const uint8_t NormalAttributeID = 1;
  SquareHandle(vector<m2::PointF> const & formingVector)
    : OverlayHandle(FeatureID(), OverlayHandle::Center, 0.0f)
    , m_vectors(formingVector)
  {
    SetIsVisible(true);
  }

  virtual m2::RectD GetPixelRect(ScreenBase const & screen) const { return m2::RectD(); }

  virtual void GetAttributeMutation(RefPointer<AttributeBufferMutator> mutator) const
  {
    static my::Timer timer;
    double angle = timer.ElapsedSeconds();

    math::Matrix<double, 3, 3> m = math::Rotate(math::Identity<double, 3>(), angle);

    vector<m2::PointF> data(4);
    for (size_t i = 0; i < m_vectors.size(); ++i)
    {
      data[i] = m_vectors[i] * m;
    }

    offset_node_t const & node = GetOffsetNode(NormalAttributeID);
    MutateNode mutateNode;
    mutateNode.m_region = node.second;
    mutateNode.m_data = MakeStackRefPointer<void>(&data[0]);
    mutator->AddMutation(node.first, mutateNode);
  }

private:
  vector<m2::PointF> m_vectors;
};

class SquareShape : public MapShape
{
public:
  SquareShape(m2::PointF const & center, float radius)
    : m_center(center)
    , m_radius(radius)
  {
  }

  virtual void Draw(RefPointer<Batcher> batcher, RefPointer<TextureSetHolder> textures) const
  {
    vector<m2::PointF> vertexes(4, m_center);

    vector<m2::PointF> formingVectors(4);
    formingVectors[0] = m2::PointF(-m_radius,  m_radius);
    formingVectors[1] = m2::PointF(-m_radius, -m_radius);
    formingVectors[2] = m2::PointF( m_radius,  m_radius);
    formingVectors[3] = m2::PointF( m_radius, -m_radius);

    AttributeProvider provider(2, 4);
    {
      BindingInfo info(1);
      BindingDecl & decl = info.GetBindingDecl(0);
      decl.m_attributeName = "a_position";
      decl.m_componentCount = 2;
      decl.m_componentType = gl_const::GLFloatType;
      decl.m_offset = 0;
      decl.m_stride = 0;
      provider.InitStream(0, info, MakeStackRefPointer<void>(&vertexes[0]));
    }
    {
      BindingInfo info(1, SquareHandle::NormalAttributeID);
      BindingDecl & decl = info.GetBindingDecl(0);
      decl.m_attributeName = "a_normal";
      decl.m_componentCount = 2;
      decl.m_componentType = gl_const::GLFloatType;
      decl.m_offset = 0;
      decl.m_stride = 0;
      provider.InitStream(1, info, MakeStackRefPointer<void>(&formingVectors[0]));
    }

    GLState state(gpu::TEST_DYN_ATTR_PROGRAM, GLState::GeometryLayer);
    state.SetColor(Color(150, 130, 120, 255));

    OverlayHandle * handle = new SquareHandle(formingVectors);

    batcher->InsertTriangleStrip(state, MakeStackRefPointer<AttributeProvider>(&provider), MovePointer(handle));
  }

private:
  m2::PointF m_center;
  float m_radius;
};

TestingEngine::TestingEngine(RefPointer<OGLContextFactory> oglcontextfactory,
                             double vs, df::Viewport const & viewport)
  : m_contextFactory(oglcontextfactory)
  , m_viewport(viewport)
{
  GLFunctions::Init();
  df::VisualParams::Init(vs, df::CalculateTileSize(viewport.GetWidth(), viewport.GetHeight()));
  m_contextFactory->getDrawContext()->makeCurrent();

  m_textures.Reset(new TextureManager());
  m_textures->Init(df::VisualParams::Instance().GetResourcePostfix());
  m_batcher.Reset(new Batcher());
  m_programManager.Reset(new GpuProgramManager());

  ModelViewInit();
  ProjectionInit();

  m_timerId = startTimer(1000 / 30);
}

TestingEngine::~TestingEngine()
{
  killTimer(m_timerId);
  ClearScene();
  m_batcher.Destroy();
  m_textures->Release();
  m_textures.Destroy();
  m_programManager.Destroy();
}

void TestingEngine::Draw()
{
  ClearScene();
  m_batcher->StartSession(bind(&df::TestingEngine::OnFlushData, this, _1, _2));
  DrawImpl();
  m_batcher->EndSession();

  OGLContext * context = m_contextFactory->getDrawContext();
  context->setDefaultFramebuffer();

  m_viewport.Apply();
  GLFunctions::glClearColor(0.65f, 0.65f, 0.65f, 1.0f);
  GLFunctions::glClear();
  GLFunctions::glDisable(gl_const::GLDepthTest);

  scene_t::iterator it = m_scene.begin();
  for(; it != m_scene.end(); ++it)
  {
    GLState const & state = it->first;
    RefPointer<GpuProgram> prg = m_programManager->GetProgram(state.GetProgramIndex());
    prg->Bind();
    TextureSetBinder binder(m_textures.GetRefPointer());
    ApplyState(state, prg, MakeStackRefPointer<TextureSetController>(&binder));
    ApplyUniforms(m_generalUniforms, prg);

    it->second->Render();
  }

  context->present();
}

void TestingEngine::Resize(int w, int h)
{
  m_viewport.SetViewport(0, 0, w, h);
  ModelViewInit();
  ProjectionInit();
  Draw();
}

void TestingEngine::DragStarted(m2::PointF const & p) {}
void TestingEngine::Drag(m2::PointF const & p) {}
void TestingEngine::DragEnded(m2::PointF const & p) {}
void TestingEngine::Scale(m2::PointF const & p, double factor) {}

void TestingEngine::timerEvent(QTimerEvent * e)
{
  if (e->timerId() == m_timerId)
    Draw();
}

void TestingEngine::DrawImpl()
{
  ReaderPtr<ModelReader> reader = GetPlatform().GetReader("test_scene.json");
  string jsonString;
  reader.ReadAsString(jsonString);

  vector<MapShape *> shapes;
  try
  {
    my::Json json(jsonString.c_str());
    MapShapeFactory factory;
    factory.CreateShapes(shapes, json.get());
  }
  catch (RootException & e)
  {
    LOG(LCRITICAL, (e.Msg()));
  }

  for (size_t i = 0; i < shapes.size(); ++i)
    shapes[i]->Draw(m_batcher.GetRefPointer(), m_textures.GetRefPointer());

  DeleteRange(shapes, DeleteFunctor());
}

void TestingEngine::ModelViewInit()
{
  float modelView[4 * 4] =
  {
    1.0, 0.0, 0.0, 0.0,
    0.0, 1.0, 0.0, 0.0,
    0.0, 0.0, 1.0, 0.0,
    0.0, 0.0, 0.0, 1.0
  };

  m_generalUniforms.SetMatrix4x4Value("modelView", modelView);
}

void TestingEngine::ProjectionInit()
{
  float left = m_viewport.GetX0();
  float right = left + m_viewport.GetWidth();
  float bottom = m_viewport.GetY0();
  float top = bottom + m_viewport.GetHeight();
  float near = -20000.0f;
  float far = 20000.0f;

  float m[4 * 4];
  memset(m, 0, sizeof(m));
  m[0]  = 2.0f / (right - left);
  m[3]  = - (right + left) / (right - left);
  m[5]  = 2.0f / (top - bottom);
  m[7]  = - (top + bottom) / (top - bottom);
  m[10] = -2.0f / (far - near);
  m[11] = - (far + near) / (far - near);
  m[15] = 1.0;

  m_generalUniforms.SetMatrix4x4Value("projection", m);
}

void TestingEngine::OnFlushData(GLState const & state, TransferPointer<RenderBucket> vao)
{
  MasterPointer<RenderBucket> bucket(vao);
  bucket->GetBuffer()->Build(m_programManager->GetProgram(state.GetProgramIndex()));
  m_scene.insert(make_pair(state, bucket));
}

void TestingEngine::ClearScene()
{
  (void)GetRangeDeletor(m_scene, MasterPointerDeleter())();
}

} // namespace df
