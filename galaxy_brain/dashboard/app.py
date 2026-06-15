"""GalaxyBrain plugin API — FastAPI app entry point for Hermes dashboard."""

from fastapi import FastAPI
from .api import router as galaxy_brain_router

app = FastAPI(title="GalaxyBrain API", version="1.0.0")
app.include_router(galaxy_brain_router, prefix="/api/plugins/galaxy-brain")